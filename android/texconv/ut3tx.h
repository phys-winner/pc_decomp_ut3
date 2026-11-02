/* UTX3 v2: sorted content keys; each payload keeps the original DXT mip chain
 * followed by ETC2 RGBA8 or ASTC 4x4. Original blocks allow later mod writes
 * to fall back without losing already uploaded mip levels or cube faces. */
#ifndef UT3_TEXTURE_CACHE_H
#define UT3_TEXTURE_CACHE_H
#include "tvtx.h"

typedef struct utx_entry {
    uint64_t key,raw_offset,data_offset;
    uint32_t raw_bytes,data_bytes,gl_format;
    uint16_t width,height,levels,dxt;
    uint32_t reserved;
} utx_entry;
static inline const utx_entry *utx_find(const utx_entry *entries,uint32_t count,uint64_t key)
{
    uint32_t lo=0,hi=count;
    while(lo<hi) { uint32_t mid=lo+(hi-lo)/2;if(entries[mid].key<key)lo=mid+1;else hi=mid; }
    return lo<count&&entries[lo].key==key ? entries+lo : NULL;
}

static inline uint64_t utx_level_size(uint32_t format,uint32_t w,uint32_t h)
{
    return (uint64_t)((w+3)/4)*((h+3)/4)*(format==0x31545844u?8:16);
}
static inline uint64_t utx_chain_size(uint32_t format,uint32_t w,uint32_t h,uint32_t levels)
{
    uint64_t bytes=0;
    for(uint32_t i=0;i<levels;i++) {
        bytes+=utx_level_size(format,w,h);w=w>1?w/2:1;h=h>1?h/2:1;
    }
    return bytes;
}
static inline uint64_t utx_key(tvtx_crc_tables *tables,uint32_t format,uint32_t w,uint32_t h,const uint8_t *data,size_t size)
{
    uint64_t meta=((uint64_t)format<<32)^((uint64_t)w<<16)^h;
    return tvtx_key(tables,data,size)^(meta*0x9E3779B185EBCA87ull);
}
#endif
