/* Read-only UT3 v512 packages and Texture2D bulk data, including LZO/zlib.
 * Every offset and decompression write is bounded before use. */
#ifndef UT3_TEXTURE_PACKAGES_H
#define UT3_TEXTURE_PACKAGES_H
#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>
#include "ut3tx.h"

namespace ut3tex {
using Bytes=std::vector<uint8_t>;
inline void require(bool valid,const char *why) { if(!valid)throw std::runtime_error(why); }
constexpr size_t MAX_PACKAGE=512u*1024*1024;
struct Reader {
    const Bytes &data;size_t pos=0;
    void room(size_t n) const { require(pos<=data.size()&&n<=data.size()-pos,"package offset outside file"); }
    uint32_t word() { room(4);uint32_t v=tvtx_le32(data.data()+pos);pos+=4;return v; }
    void skip(size_t n) { room(n);pos+=n; }
    std::string text() {
        int32_t n=(int32_t)word();require(n>=-32768&&n<=65536,"invalid package string");
        size_t bytes=n<0?(size_t)-n*2:(size_t)n;room(bytes);
        std::string out;
        for(size_t i=0;i<(n<0?(size_t)-n:(size_t)n);i++) {
            uint32_t c=data[pos+i*(n<0?2:1)];if(!c)break;
            out.push_back(c<128?(char)c:'?');
        }
        pos+=bytes;return out;
    }
};

inline Bytes lzo(const uint8_t *src,size_t size,size_t expected)
{
    require(size>=3&&expected<=MAX_PACKAGE,"invalid LZO block");
    Bytes out;out.reserve(expected);size_t ip=0;
    auto get=[&]()->size_t { require(ip<size,"truncated LZO input");return src[ip++]; };
    auto literal=[&](size_t n) {
        require(n<=size-ip&&n<=expected-out.size(),"LZO literal overflow");
        out.insert(out.end(),src+ip,src+ip+n);ip+=n;
    };
    auto copy=[&](size_t distance,size_t n) {
        require(distance>0&&distance<=out.size()&&n<=expected-out.size(),"LZO match overflow");
        for(size_t k=0;k<n;k++)out.push_back(out[out.size()-distance]);
    };
    auto length=[&](size_t n) {
        size_t v;
        while((v=get())==0) { require(n<=expected&&255<=expected-n,"LZO length overflow");n+=255; }
        require(n<=expected&&v<=expected-n,"LZO length overflow");return n+v;
    };
    int state=0;size_t t=0;
    if(src[0]>17) { t=get()-17;if(t<4)state=4;else { literal(t);state=1; } }
    for(;;) {
        if(state==0) { t=get();if(t>=16){state=2;continue;}if(!t)t=length(15);literal(t+3);state=1; }
        if(state==1) { t=get();if(t>=16)state=2;else { size_t b=get();copy(1+0x800+(t>>2)+(b<<2),3);state=3; } }
        if(state==2) {
            if(t>=64) { size_t b=get();copy(1+((t>>2)&7)+(b<<3),(t>>5)+1); }
            else if(t>=32) { t&=31;if(!t)t=length(31);size_t a=get(),b=get();copy(1+(a>>2)+(b<<6),t+2); }
            else if(t>=16) {
                size_t distance=(t&8)<<11;t&=7;if(!t)t=length(7);
                size_t a=get(),b=get();distance+=(a>>2)+(b<<6);
                if(!distance) { require(out.size()==expected&&ip==size,"invalid LZO terminator");return out; }
                copy(distance+0x4000,t+2);
            } else { size_t b=get();copy(1+(t>>2)+(b<<2),2); }
            state=3;
        }
        if(state==3) { require(ip>=2,"invalid LZO state");t=src[ip-2]&3;if(!t){state=0;continue;}state=4; }
        if(state==4) { literal(t);t=get();state=2; }
    }
}

inline Bytes compressed(const Bytes &source,size_t offset,size_t stored,size_t expected,uint32_t codec)
{
    require(offset<=source.size()&&stored<=source.size()-offset&&expected<=MAX_PACKAGE,"invalid compressed payload");
    Reader r{source,offset};require(r.word()==0x9E2A83C1u,"invalid compressed chunk magic");
    uint32_t block=r.word();r.word();uint32_t total=r.word();
    require(block>0&&block<=MAX_PACKAGE&&total==expected,"invalid compressed chunk size");
    std::vector<std::pair<uint32_t,uint32_t>> sizes;
    size_t remaining=total;
    while(remaining) {
        uint32_t c=r.word(),u=r.word();require(u>0&&u<=block&&u<=remaining&&c>0,"invalid compressed block size");
        sizes.emplace_back(c,u);remaining-=u;
    }
    Bytes out;out.reserve(total);
    for(auto [c,u]:sizes) {
        require(r.pos>=offset&&r.pos-offset<=stored&&c<=stored-(r.pos-offset),"compressed block outside payload");
        Bytes decoded;
        if(codec&2)decoded=lzo(source.data()+r.pos,c,u);
        else if(codec&1) {
            decoded.resize(u);uLongf n=u;
            require(uncompress(decoded.data(),&n,source.data()+r.pos,c)==Z_OK&&n==u,"invalid zlib block");
        } else throw std::runtime_error("unsupported package compression");
        out.insert(out.end(),decoded.begin(),decoded.end());r.skip(c);
    }
    require(r.pos==offset+stored,"compressed payload size mismatch");return out;
}

struct Export { int32_t type;uint32_t size,offset; };
struct Package {
    Bytes data;std::vector<std::string> names,imports;std::vector<Export> exports;
    static Bytes read(const std::string &path) {
        std::ifstream file(path,std::ios::binary|std::ios::ate);require((bool)file,"cannot open package");
        auto size=file.tellg();require(size>=0&&(uint64_t)size<=MAX_PACKAGE,"package exceeds 512 MiB");
        Bytes raw((size_t)size);file.seekg(0);require((bool)file.read((char*)raw.data(),size),"cannot read package");
        return raw;
    }
    explicit Package(const std::string &path):Package(read(path)) {}
    explicit Package(Bytes raw) {
        require(raw.size()<=MAX_PACKAGE,"package exceeds 512 MiB");
        Reader r{raw};require(r.word()==0x9E2A83C1u,"invalid package magic");
        require((r.word()&65535)==512,"unsupported UT3 package version");r.word();r.text();r.word();
        uint32_t nc=r.word(),no=r.word(),ec=r.word(),eo=r.word(),ic=r.word(),io=r.word();r.word();r.skip(16);
        uint32_t generations=r.word();require(generations<=1024,"invalid package generations");r.skip(generations*12);
        r.word();r.word();uint32_t codec=r.word(),count=r.word();require(count<=4096,"too many package chunks");
        struct Chunk { uint32_t u,s,c,n; };std::vector<Chunk> chunks;size_t total=raw.size();
        for(uint32_t i=0;i<count;i++) { Chunk c{r.word(),r.word(),r.word(),r.word()};
            require(c.u<=MAX_PACKAGE&&c.s<=MAX_PACKAGE-c.u,"package decompression exceeds 512 MiB");
            total=std::max(total,(size_t)c.u+c.s);chunks.push_back(c);
        }
        if(chunks.empty())data=std::move(raw);
        else {
            require(chunks.front().u<=raw.size()&&total<=MAX_PACKAGE,"invalid package header range");
            data.resize(total);memcpy(data.data(),raw.data(),chunks.front().u);
            size_t previous=chunks.front().u;
            for(auto c:chunks) { require(c.u==previous,"noncontiguous package chunks");
                Bytes bytes=compressed(raw,c.c,c.n,c.s,codec);memcpy(data.data()+c.u,bytes.data(),bytes.size());previous=c.u+c.s;
            }
        }
        require(nc<=1048576&&ic<=1048576&&ec<=1048576,"package table exceeds limit");
        Reader table{data,no};
        for(uint32_t i=0;i<nc;i++){names.push_back(table.text());table.skip(8);}
        table.pos=io;
        for(uint32_t i=0;i<ic;i++) { table.skip(20);imports.push_back(name(table.word()));table.word(); }
        table.pos=eo;
        for(uint32_t i=0;i<ec;i++) {
            int32_t type=(int32_t)table.word();table.skip(28);uint32_t bytes=table.word(),at=table.word();
            uint32_t components=table.word();require(components<=1048576,"invalid component table");table.skip(components*12);
            table.word();uint32_t generations2=table.word();require(generations2<=1024,"invalid export generations");table.skip(generations2*4+20);
            require(at<=data.size()&&bytes<=data.size()-at,"export outside package");exports.push_back({type,bytes,at});
        }
    }
    const std::string &name(uint32_t index) const { require(index<names.size(),"invalid name index");return names[index]; }
};
struct Mip { uint32_t w,h;Bytes blocks; };
struct Texture { uint32_t format;std::vector<Mip> mips; };

inline void textures(const Package &p,const std::function<void(const Texture&)> &visit)
{
    for(auto e:p.exports) {
        if(e.type>=0||uint32_t(-int64_t(e.type))>p.imports.size()||p.imports[-int64_t(e.type)-1]!="Texture2D")continue;
        Reader r{p.data,e.offset};r.word();uint32_t format=0,width=0,height=0;
        for(uint32_t tags=0;;tags++) {
            require(tags<65536&&r.pos<=(uint64_t)e.offset+e.size,"invalid texture properties");
            std::string n=p.name(r.word());r.word();if(n=="None")break;
            std::string type=p.name(r.word());r.word();uint32_t size=r.word();r.word();
            if(type=="StructProperty")r.skip(8);
            if(type=="BoolProperty")r.skip(4);
            if((n=="SizeX"||n=="SizeY")&&size==4) {
                uint32_t value=r.word();if(n=="SizeX")width=value;else height=value;
            } else if(n=="Format"&&size==8) {
                std::string f=p.name(r.word());r.word();
                format=f=="PF_DXT1"?0x31545844u:f=="PF_DXT3"?0x33545844u:f=="PF_DXT5"?0x35545844u:0;
            } else r.skip(size);
        }
        if(!format)continue;
        auto bulk=[&](bool decode)->Bytes {
            uint32_t flags=r.word(),count=r.word(),stored=r.word(),offset=r.word();
            require(count<=MAX_PACKAGE,"texture bulk exceeds limit");
            // Seek-free packages reference mip payloads in the original source UPK.
            // Those source packages are scanned too; external references contain no inline bytes.
            if((flags&33)||!count)return {};
            require(offset==r.pos&&stored<=p.data.size()-r.pos,"invalid inline texture bulk");
            Bytes out;
            if(decode) {
                if(flags&18)out=compressed(p.data,r.pos,stored,count,(flags&16)?2:1);
                else { require(stored==count,"raw texture size mismatch");out.assign(p.data.begin()+r.pos,p.data.begin()+r.pos+stored); }
            }
            r.skip(stored);return out;
        };
        bulk(false);uint32_t count=r.word();require(count<=16,"invalid texture mip count");Texture texture{format,{}};
        for(uint32_t i=0;i<count;i++) {
            Bytes blocks=bulk(true);uint32_t w=r.word(),h=r.word();
            require(w>0&&h>0&&w<=16384&&h<=16384,"invalid texture dimensions");
            if(!width)width=w<<i;if(!height)height=h<<i;
            uint32_t logical_w=std::max(1u,width>>i),logical_h=std::max(1u,height>>i);
            // UT3 serializes the final BC mip dimensions as 4 even for logical 2 and 1.
            require((w==logical_w||w==std::max(4u,logical_w))&&(h==logical_h||h==std::max(4u,logical_h)),"texture mip dimensions disagree with SizeX/SizeY");
            w=logical_w;h=logical_h;
            if(blocks.empty()) { require(texture.mips.empty(),"missing interior texture mip");continue; }
            require(utx_level_size(format,w,h)==blocks.size(),"texture mip size mismatch");
            if(!texture.mips.empty()) { const auto &prev=texture.mips.back();
                require(w==std::max(1u,prev.w/2)&&h==std::max(1u,prev.h/2),"noncontiguous texture mips"); }
            texture.mips.push_back({w,h,std::move(blocks)});
        }
        require(r.pos<=(uint64_t)e.offset+e.size,"texture data outside export");
        if(!texture.mips.empty())visit(texture);
    }
}
}
#endif
