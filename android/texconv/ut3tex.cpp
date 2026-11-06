/* Optional, read-only Texture2D conversion before starting the game. */
#include "ue3.h"
#include "texconv.h"
#include "../launcher/native/zipmount.h"
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <memory>
#include <map>
#include <cerrno>
#include <cctype>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <jni.h>

namespace {
using namespace ut3tex;
namespace fs=std::filesystem;
std::atomic<bool> cancelled{false};
std::atomic<int64_t> done{0},total{0},bytes_done{0},bytes_total{0};
constexpr int64_t ORIGINAL_TEXTURES=34833,ORIGINAL_MIPS=253302,ORIGINAL_BYTES=2879901632LL;
std::mutex message_lock;
std::string message;
void error(const std::string &why) { std::lock_guard<std::mutex> guard(message_lock);message=why; }
std::string lower(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return (char)std::tolower(c);});return value;
}
fs::path child(const fs::path &parent,const std::string &name) {
    if(fs::exists(parent/name))return parent/name;
    if(!fs::is_directory(parent))return parent/name;
    for(const auto &entry:fs::directory_iterator(parent))if(lower(entry.path().filename().string())==lower(name))return entry.path();
    return parent/name;
}
std::string pack(const std::string &game) { return child(child(game,"UTGame"),TVTX_FILE).string(); }
struct Source { std::string path;const zm_entry *entry=nullptr; };
struct Game {
    std::string root;
    std::unique_ptr<zm_mount,decltype(&zm_close)> archive{nullptr,zm_close};
    Game(const std::string &game,const std::string &index,int fd):root(game) {
        if(!index.empty()) {
            archive.reset(zm_open(fd,index.c_str(),game.c_str()));
            require(archive!=nullptr,"game archive index is invalid or the archive has changed; select it again");
        }
    }
    Package read(const Source &source) const {
        if(!source.entry)return Package(source.path);
        if(source.entry->flags&ZM_SIDECAR)return Package((fs::path(root)/source.path).string());
        require(source.entry->size<=MAX_PACKAGE,"package exceeds 512 MiB");
        Bytes raw((size_t)source.entry->size);size_t at=0;
        while(at<raw.size()) {
            if(cancelled)throw std::runtime_error("Cancelled");
            auto n=zm_pread(archive.get(),source.entry,raw.data()+at,raw.size()-at,at);
            require(n>0,"cannot read package from game archive");at+=(size_t)n;
        }
        return Package(std::move(raw));
    }
};
bool package_path(const fs::path &path) {
    std::string ext=lower(path.extension().string());return ext==".upk"||ext==".ut3"||ext==".u";
}
std::vector<Source> packages(const Game &game) {
    std::map<std::string,Source> found;
    auto cooked=child(child(game.root,"UTGame"),"CookedPC");
    if(fs::is_directory(cooked)) {
        for(const auto &entry:fs::recursive_directory_iterator(cooked)) {
            if(cancelled)throw std::runtime_error("Cancelled");
            if(entry.is_regular_file()&&package_path(entry.path()))
                found.emplace(lower(entry.path().lexically_relative(cooked).generic_string()),Source{entry.path().string()});
        }
    } else require(game.archive!=nullptr,"UTGame/CookedPC folder is missing or unreadable");
    // Physical sidecar files override archive entries, as in the game's own file layer.
    zm_list(game.archive.get(),"UTGame/CookedPC",[](const zm_entry *entry,void *user) {
        if(cancelled)return 1;
        std::string path(entry->path,entry->length);
        if(package_path(path)) {
            auto &found=*(std::map<std::string,Source>*)user;
            found.emplace(lower(path.substr(sizeof("UTGame/CookedPC/")-1)),Source{path,entry});
        }
        return 0;
    },&found);
    if(cancelled)throw std::runtime_error("Cancelled");
    std::vector<Source> paths;
    for(auto &item:found)paths.push_back(std::move(item.second));
    return paths;
}
struct Output {
    int fd=-1;uint64_t at=sizeof(tvtx_header);
    explicit Output(const std::string &path) {
        fd=open(path.c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0644);require(fd>=0,"cannot create texture cache");
        // Android's emulated storage supports OFD record locks, but can reject flock with ENOSYS.
        struct flock lock{};lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
        int result=fcntl(fd,F_OFD_SETLK,&lock);const char *operation="lock";
        if(!result) { operation="reset";result=ftruncate(fd,0); }
        if(result) {
            int reason=errno;close(fd);fd=-1;
            throw std::runtime_error(std::string("cannot ")+operation+" texture cache: "+strerror(reason));
        }
    }
    ~Output() { if(fd>=0)close(fd); }
    void write(const void *data,size_t size,uint64_t offset) {
        const uint8_t *p=(const uint8_t*)data;
        while(size) { ssize_t n=pwrite(fd,p,size,offset);if(n<0&&errno==EINTR)continue;
            require(n>0,"cannot write texture cache (storage full?)");p+=n;size-=n;offset+=n; }
    }
    uint64_t append(const void *data,size_t size) { uint64_t offset=at;write(data,size,at);at+=size;return offset; }
};
Bytes dds(const Texture &texture) {
    const auto &m=texture.mips.front();Bytes out(128,0);
    auto set=[&](size_t at,uint32_t value){memcpy(out.data()+at,&value,4);};
    set(0,0x20534444);set(4,124);set(8,0x000A1007);set(12,m.h);set(16,m.w);
    set(20,(uint32_t)m.blocks.size());set(28,(uint32_t)texture.mips.size());set(76,32);set(80,4);
    set(84,texture.format);set(108,0x401008);
    for(const auto &mip:texture.mips)out.insert(out.end(),mip.blocks.begin(),mip.blocks.end());
    return out;
}
bool read_header(const std::string &path,tvtx_header &h,uint64_t &size) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return false;
    auto end=f.tellg();if(end<(std::streamoff)sizeof(h))return false;size=(uint64_t)end;f.seekg(0);f.read((char*)&h,sizeof(h));
    return (bool)f&&h.magic==TVTX_MAGIC&&h.version==TVTX_VERSION&&(h.family==1||h.family==2)&&
        h.count>0&&h.count<=2000000&&h.index_offset>=sizeof(h)&&h.index_offset<=size&&
        h.payload_bytes==h.index_offset-sizeof(h)&&
        uint64_t(h.count)*sizeof(utx_entry)==size-h.index_offset;
}
std::vector<int64_t> quick_scan(const std::string &game) {
    // Original retail corpus: 34,833 Texture2Ds, 253,302 mips; conversion still discovers the actual packages.
    tvtx_header h{};uint64_t size=0;bool converted=read_header(pack(game),h,size);
    return {ORIGINAL_TEXTURES,ORIGINAL_BYTES,converted?(int64_t)h.family:0,converted?(int64_t)h.count:0,converted?(int64_t)size:0};
}
std::vector<int64_t> scan(const std::string &game,const std::string &index="",int fd=-1) {
    done=total=bytes_done=bytes_total=0;cancelled=false;error("");
    Game source(game,index,fd);
    for(const auto &path:packages(source)) {
        try {
            Package p=source.read(path);
            textures(p,[&](const Texture &texture){for(const auto &m:texture.mips){++total;bytes_total+=m.blocks.size();}});
        } catch(const std::exception &e) { throw std::runtime_error(fs::path(path.path).filename().string()+": "+e.what()); }
        if(cancelled)throw std::runtime_error("Cancelled");
    }
    tvtx_header h{};uint64_t size=0;bool converted=read_header(pack(game),h,size);
    return {total,bytes_total,converted?(int64_t)h.family:0,converted?(int64_t)h.count:0,converted?(int64_t)size:0};
}
int convert(const std::string &game,int family,int threads,const std::string &archive_index="",int fd=-1) {
    require(family==1||family==2,"unknown texture format");
    cancelled=false;done=bytes_done=0;total=ORIGINAL_MIPS;bytes_total=ORIGINAL_BYTES;error("");
    Game source(game,archive_index,fd);
    auto paths=packages(source);require(!paths.empty(),"no UT3 packages found");
    std::string final=pack(game),temporary=final+".part";
    fs::create_directories(fs::path(final).parent_path());
    /* A file lock permits recovery of .part after a killed process while excluding another live converter. */
    Output output(temporary);std::mutex write_lock;std::vector<utx_entry> index;
    std::atomic<size_t> next{0};std::atomic<bool> broken{false};
    uint64_t source_bytes=0;
    auto worker=[&] {
        tvtx_crc_tables crc{};
        for(;;) {
            size_t i=next++;if(i>=paths.size()||cancelled||broken)return;
            try {
                Package p=source.read(paths[i]);
                textures(p,[&](const Texture &texture) {
                    if(cancelled||broken)return;
                    Bytes source=dds(texture);uint8_t *encoded=nullptr;uint64_t encoded_size=0;uint32_t format,flags;
                    require(texconv_convert_dds(source.data(),source.size(),family,&encoded,&encoded_size,&format,&flags)==0,"texture encoder failed");
                    std::unique_ptr<uint8_t,decltype(&texconv_free)> owner(encoded,texconv_free);
                    std::lock_guard<std::mutex> guard(write_lock);
                    uint64_t raw=output.append(source.data()+128,source.size()-128),data=output.append(encoded,(size_t)encoded_size);
                    uint64_t raw_left=source.size()-128,data_left=encoded_size;
                    for(size_t l=0;l<texture.mips.size();l++) {
                        const auto &m=texture.mips[l];
                        index.push_back({utx_key(&crc,texture.format,m.w,m.h,m.blocks.data(),m.blocks.size()),raw,data,
                            (uint32_t)raw_left,(uint32_t)data_left,format,(uint16_t)m.w,(uint16_t)m.h,
                            (uint16_t)(texture.mips.size()-l),(uint16_t)(texture.format==0x31545844u?1:texture.format==0x33545844u?3:5),0});
                        uint64_t bytes=tvtx_level_bytes(format,m.w,m.h);
                        raw+=m.blocks.size();data+=bytes;raw_left-=m.blocks.size();data_left-=bytes;
                        ++done;bytes_done+=m.blocks.size();
                    }
                    source_bytes+=source.size()-128;
                });
            } catch(const std::exception &e) {
                if(!broken.exchange(true))error(fs::path(paths[i].path).filename().string()+": "+e.what());return;
            }
        }
    };
    try {
        std::vector<std::thread> pool;
        for(int i=0;i<std::max(1,std::min(threads,2));i++)pool.emplace_back(worker);
        for(auto &thread:pool)thread.join();
        require(!cancelled,"Cancelled");require(!broken,"texture conversion failed; see details");require(!index.empty(),"no DXT Texture2D mips found");
        std::sort(index.begin(),index.end(),[](const utx_entry &a,const utx_entry &b){return a.key<b.key;});
        index.erase(std::unique(index.begin(),index.end(),[](const utx_entry &a,const utx_entry &b){return a.key==b.key;}),index.end());
        tvtx_header h{};h.magic=TVTX_MAGIC;h.version=TVTX_VERSION;h.family=family;h.count=(uint32_t)index.size();
        h.index_offset=output.at;h.payload_bytes=output.at-sizeof(h);h.source_bytes=source_bytes;h.sources=(uint32_t)done;
        output.append(index.data(),index.size()*sizeof(utx_entry));output.write(&h,sizeof(h),0);
        require(fsync(output.fd)==0,"cannot flush texture cache");
        require(rename(temporary.c_str(),final.c_str())==0,"cannot commit texture cache");
        require(close(output.fd)==0,"cannot close texture cache");output.fd=-1;
        total=done.load();bytes_total=bytes_done.load();
        return 0;
    } catch(...) { unlink(temporary.c_str());throw; }
}
std::string java_string(JNIEnv *env,jstring value) {
    require(value!=nullptr,"missing game path");const char *s=env->GetStringUTFChars(value,nullptr);require(s!=nullptr,"cannot read game path");
    std::string out(s);env->ReleaseStringUTFChars(value,s);return out;
}
}

extern "C" JNIEXPORT jlongArray JNICALL Java_org_ut3_port_TextureConverter_nativeScan(JNIEnv *env,jclass,jstring game) {
    try { auto values=quick_scan(java_string(env,game));jlongArray out=env->NewLongArray(values.size());if(out)env->SetLongArrayRegion(out,0,values.size(),(const jlong*)values.data());return out; }
    catch(const std::exception &e){error(e.what());return nullptr;}
}
extern "C" JNIEXPORT jint JNICALL Java_org_ut3_port_TextureConverter_nativeConvert(JNIEnv *env,jclass,jstring game,jint family,jint threads,jstring index,jint fd) {
    try { return convert(java_string(env,game),family,threads,index?java_string(env,index):"",fd); }
    catch(const std::exception &e){std::lock_guard<std::mutex> guard(message_lock);if(message.empty())message=e.what();return -1;}
}
extern "C" JNIEXPORT jint JNICALL Java_org_ut3_port_TextureConverter_nativeUndo(JNIEnv *env,jclass,jstring game) {
    try { std::string path=pack(java_string(env,game));return unlink(path.c_str())==0||errno==ENOENT?0:-1; }
    catch(const std::exception &e){error(e.what());return -1;}
}
extern "C" JNIEXPORT void JNICALL Java_org_ut3_port_TextureConverter_nativeCancel(JNIEnv*,jclass) { cancelled=true; }
extern "C" JNIEXPORT jlongArray JNICALL Java_org_ut3_port_TextureConverter_nativeProgress(JNIEnv *env,jclass) {
    jlong values[]={done,total,bytes_done,bytes_total};jlongArray out=env->NewLongArray(4);if(out)env->SetLongArrayRegion(out,0,4,values);return out;
}
extern "C" JNIEXPORT jstring JNICALL Java_org_ut3_port_TextureConverter_nativeMessage(JNIEnv *env,jclass) {
    std::lock_guard<std::mutex> guard(message_lock);return env->NewStringUTF(message.c_str());
}
