/* Optional launcher cache; every miss continues through the normal DXT decode. */
#include "host.h"
#include "../texconv/ut3tx.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const uint8_t *pack;
static const utx_entry *entries;
static uint32_t count;
static tvtx_crc_tables tables;
_Static_assert(sizeof(utx_entry)==48,"UTX3 entry layout");

void void_texture_cache_init(const char *game)
{
    char path[4096];
    if(pack||snprintf(path,sizeof(path),"%s/UTGame/" TVTX_FILE,game)>=(int)sizeof(path))return;
    char *resolved=void_path_to_host("C:\\Games\\UT3\\UTGame\\" TVTX_FILE);
    int fd=open(resolved?resolved:path,O_RDONLY|O_CLOEXEC);free(resolved);if(fd<0)return;
    struct stat st;tvtx_header h;
    if(fstat(fd,&st)||st.st_size<(off_t)sizeof(h)||pread(fd,&h,sizeof(h),0)!=sizeof(h)) { close(fd);return; }
    uint64_t size=(uint64_t)st.st_size;
    if(h.magic!=TVTX_MAGIC||h.version!=TVTX_VERSION||(h.family!=1&&h.family!=2)||h.count>2000000||
       h.index_offset<sizeof(h)||(h.index_offset&7)||h.index_offset>size||
       (uint64_t)h.count*sizeof(utx_entry)!=size-h.index_offset) { close(fd);void_log("[textures] invalid cache header; using DXT");return; }
    const uint8_t *data=mmap(NULL,(size_t)size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
    if(data==MAP_FAILED)return;
    const utx_entry *index=(const utx_entry*)(data+h.index_offset);
    for(uint32_t i=0;i<h.count;i++) {
        const utx_entry *e=index+i;
        uint32_t dxt=e->dxt==1?0x31545844u:e->dxt==3?0x33545844u:e->dxt==5?0x35545844u:0;
        uint32_t format=e->gl_format,levels=1,w=e->width,he=e->height;
        while(w>1||he>1) { w=w>1?w/2:1;he=he>1?he/2:1;levels++; }
        uint64_t encoded=0;w=e->width;he=e->height;
        for(unsigned l=0;l<e->levels&&l<16;l++){encoded+=tvtx_level_bytes(format,w,he);w=w>1?w/2:1;he=he>1?he/2:1;}
        if(!dxt||!e->width||!e->height||e->width>16384||e->height>16384||!e->levels||e->levels>levels||
           (format!=TVTX_GL_ETC2_RGB8&&format!=TVTX_GL_ETC2_RGBA8&&format!=TVTX_GL_ASTC_4x4)||
           e->raw_offset<sizeof(h)||e->raw_offset>h.index_offset||e->raw_bytes>h.index_offset-e->raw_offset||
           e->data_offset<sizeof(h)||e->data_offset>h.index_offset||e->data_bytes>h.index_offset-e->data_offset||
           e->raw_bytes!=utx_chain_size(dxt,e->width,e->height,e->levels)||e->data_bytes!=encoded||
           (i&&e->key<=index[i-1].key)) {
            munmap((void*)data,(size_t)size);void_log("[textures] invalid cache index; using DXT");return;
        }
    }
    tvtx_crc_init(&tables);pack=data;entries=index;count=h.count;
    void_log("[textures] optional %s cache: %u mip keys, %llu bytes",h.family==2?"ASTC":"ETC2",count,(unsigned long long)size);
}

const utx_entry *void_texture_cache_find(uint32_t format,uint32_t w,uint32_t h,uint32_t levels,const uint8_t *data,uint32_t pitch)
{
    if(!pack||(format!=0x31545844u&&format!=0x33545844u&&format!=0x35545844u)||
       pitch!=((w+3)/4)*(format==0x31545844u?8:16))return NULL;
    uint64_t bytes=utx_level_size(format,w,h);
    const utx_entry *e=utx_find(entries,count,utx_key(&tables,format,w,h,data,(size_t)bytes));
    if(!e||e->width!=w||e->height!=h||e->levels<levels)return NULL;
    if(utx_key(&tables,format,w,h,pack+e->raw_offset,(size_t)bytes)!=e->key)return NULL;
    return e;
}
const uint8_t *void_texture_cache_raw(const utx_entry *e) { return pack+e->raw_offset; }
const uint8_t *void_texture_cache_data(const utx_entry *e) { return pack+e->data_offset; }
