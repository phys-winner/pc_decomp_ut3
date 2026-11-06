/* Android self-check of production package parsing, codecs and cache writing. */
#include "../android/texconv/ut3tex.cpp"
#include <cassert>
#include <signal.h>
#include <sys/wait.h>

static void check_locks(const std::string &directory)
{
    fs::create_directories(directory);auto path=(fs::path(directory)/"lock-test.part").string();
    require(!fs::exists(path),"lock test file already exists");
    {
        Output active(path);active.write("keep",4,0);
        bool refused=false;
        try{Output other(path);}catch(const std::exception &e){refused=std::string(e.what()).find("cannot lock")!=std::string::npos;}
        assert(refused && Package::read(path)==Bytes({'k','e','e','p'}));
        pid_t child=fork();assert(child>=0);
        if(!child) {
            close(active.fd);bool busy=false;
            try{Output other(path);}catch(const std::exception &){busy=true;}
            _exit(busy?0:1);
        }
        int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
        assert(Package::read(path)==Bytes({'k','e','e','p'}));
    }
    int ready[2];assert(!pipe(ready));pid_t child=fork();assert(child>=0);
    if(!child) {
        close(ready[0]);Output active(path);active.write("stale",5,0);
        assert(write(ready[1],"x",1)==1);for(;;)pause();
    }
    close(ready[1]);char value;assert(read(ready[0],&value,1)==1);close(ready[0]);
    assert(!kill(child,SIGKILL));int status;assert(waitpid(child,&status,0)==child && WIFSIGNALED(status));
    { Output recovered(path);assert(fs::file_size(path)==0); }
    assert(!unlink(path.c_str()));
    puts("PASS OFD cache lock: same-process and cross-process exclusion preserve active bytes, killed writer releases lock and stale partial recovers");
}

int main(int argc,char **argv)
{
    using namespace ut3tex;
    const uint8_t literal[]={21,1,2,3,4,17,0,0};
    assert((lzo(literal,sizeof literal,4)==Bytes{1,2,3,4}));
    bool refused=false;try{lzo(literal,sizeof literal,3);}catch(const std::exception&){refused=true;}assert(refused);
    refused=false;try{lzo(literal,3,4);}catch(const std::exception&){refused=true;}assert(refused);
    Texture texture{0x31545844u,{{4,4,Bytes{0,248,0,248,0,0,0,0}},{2,2,Bytes{0,248,0,248,0,0,0,0}}}};
    Bytes source=dds(texture);tvtx_dds parsed;assert(tvtx_dds_bytes(source.data(),source.size(),&parsed)&&parsed.mips==2);
    for(int family:{1,2}) {
        uint8_t *encoded=nullptr;uint64_t size=0;uint32_t format=0,flags=0;
        assert(texconv_convert_dds(source.data(),source.size(),family,&encoded,&size,&format,&flags)==0);
        assert(size==tvtx_level_bytes(format,4,4)+tvtx_level_bytes(format,2,2));
        uint8_t rgba[64];assert(texconv_decode_converted(format,encoded,4,4,rgba)==0);
        for(unsigned i=0;i<16;i++){assert(rgba[4*i]>240&&rgba[4*i+1]<16&&rgba[4*i+2]<16&&rgba[4*i+3]>240);}
        texconv_free(encoded);
    }
    puts("PASS: bounded LZO, truncated/malformed input, DXT mip chain, ETC2/ASTC encode and decode");
    if(argc>=3) {
        try {
            if(std::string(argv[1])=="--convert") {
                assert(argc==4);auto before=scan(argv[2]);printf("scan: %lld mips, %lld DXT bytes\n",(long long)before[0],(long long)before[1]);
                require(convert(argv[2],atoi(argv[3]),2)==0,"conversion failed");
                auto after=scan(argv[2]);assert(after[2]==atoi(argv[3])&&after[3]>0);
                printf("converted: %lld unique mips, %lld cache bytes\n",(long long)after[3],(long long)after[4]);
            } else if(std::string(argv[1])=="--lock") { check_locks(argv[2]); }
            else { auto v=scan(argv[2]);printf("scan: %lld mips, %lld DXT bytes\n",(long long)v[0],(long long)v[1]); }
        } catch(const std::exception &e) { fprintf(stderr,"FAIL: %s (%s)\n",e.what(),message.c_str());return 1; }
    }
    return 0;
}
