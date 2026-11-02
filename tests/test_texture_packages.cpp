/* Android self-check of production package parsing, codecs and cache writing. */
#include "../android/texconv/ut3tex.cpp"
#include <cassert>

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
            } else { auto v=scan(argv[2]);printf("scan: %lld mips, %lld DXT bytes\n",(long long)v[0],(long long)v[1]); }
        } catch(const std::exception &e) { fprintf(stderr,"FAIL: %s (%s)\n",e.what(),message.c_str());return 1; }
    }
    return 0;
}
