"""Run the production converter's folder/ZIP routing on Windows, without adb.

The encoder is a deterministic stand-in: this checks package reads, overlays,
cache transactions and JNI build compatibility, not GPU sampling or compression.
"""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zipfile

from tests.host_environment import CLANG, environment

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/texture-source-test'


def package(color):
    def words(*values):
        return struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values))

    names = ['None', 'Texture2D', 'Format', 'ByteProperty', 'PF_DXT1', 'SizeX', 'SizeY', 'IntProperty']
    raw = bytearray(words(0x9e2a83c1, 512, 0, 1) + b'\0' + words(0))
    tables = len(raw)
    raw += bytes(28 + 16) + words(0, 0, 0, 0, 0)
    name_at = len(raw)
    for name in names:
        raw += words(len(name) + 1) + name.encode() + bytes(9)
    import_at = len(raw)
    raw += bytes(20) + words(1, 0)
    export_at = len(raw)
    raw += words(-1) + bytes(68)
    data_at = len(raw)
    data = bytearray(words(0))
    data += words(2, 0, 3, 0, 8, 0, 4, 0)
    data += words(5, 0, 7, 0, 4, 0, 4)
    data += words(6, 0, 7, 0, 4, 0, 4)
    data += words(0, 0) + words(0, 0, 0, 0) + words(1)
    data += words(0, 8, 8, data_at + len(data) + 16)
    data += bytes([color, 248, color, 248, 0, 0, 0, 0]) + words(4, 4)
    struct.pack_into('<7I', raw, tables, len(names), name_at, 1, export_at, 1, import_at, 0)
    struct.pack_into('<2I', raw, export_at + 32, len(data), data_at)
    return bytes(raw + data)


def archive(folder, mixed=False):
    path, index = folder / ('mixed.zip' if mixed else 'stored.zip'), folder / ('mixed.zmi' if mixed else 'stored.zmi')
    entries = [('UTGame/CookedPC/Characters/Test.upk', package(1), False),
               ('UTGame/CookedPC/Maps/Map.UT3', package(2), False)]
    if mixed:
        entries.append(('UTGame/CookedPC/Script/Compressed.u', package(3), True))
    with zipfile.ZipFile(path, 'w') as z:
        for name, data, sidecar in entries:
            z.writestr('Steam/' + name, data, zipfile.ZIP_DEFLATED if sidecar else zipfile.ZIP_STORED)
        z.writestr('Steam/Engine/ignored.upk', b'not a game package')
    data = bytearray(b'ZMI1' + struct.pack('<IQ', len(entries), path.stat().st_size) + bytes(32))
    with zipfile.ZipFile(path) as z:
        for name, content, sidecar in entries:
            info = z.getinfo('Steam/' + name)
            offset = info.header_offset + 30 + len(info.filename.encode()) + len(info.extra)
            name = name.encode()
            data += struct.pack('<QQIIQH', offset, len(content), info.CRC, int(sidecar), 0, len(name)) + name
    index.write_bytes(data)
    return path, index


class TextureSourceTest(unittest.TestCase):
    def test_folder_and_mounted_archive(self):
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / 'sys').mkdir(exist_ok=True)
        (OUT / 'sys/file.h').write_text('#define LOCK_EX 1\n#define LOCK_NB 2\n', encoding='ascii')
        (OUT / 'zlib.h').write_text('typedef unsigned long uLongf;\n#define Z_OK 0\n'
            'inline int uncompress(unsigned char*,uLongf*,const unsigned char*,unsigned long){return -3;}\n', encoding='ascii')
        (OUT / 'unistd.h').write_text(r'''
#include <io.h>
#include <fcntl.h>
#include <sys/locking.h>
#include <windows.h>
#include <cstddef>
using ssize_t=std::ptrdiff_t;
#define O_CLOEXEC 0
static int fixture_open(const char *path,int flags,int=0) {
    HANDLE h=CreateFileA(path,(flags&_O_RDWR)?GENERIC_READ|GENERIC_WRITE:GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,(flags&_O_CREAT)?OPEN_ALWAYS:OPEN_EXISTING,0,nullptr);
    return h==INVALID_HANDLE_VALUE?-1:_open_osfhandle((intptr_t)h,_O_BINARY|flags);
}
static ssize_t fixture_pwrite(int fd,const void *data,size_t size,uint64_t at) {
    return _lseeki64(fd,at,SEEK_SET)<0?-1:_write(fd,data,(unsigned)size);
}
static int fixture_flock(int fd,int) { return _locking(fd,_LK_NBLCK,1); }
static int fixture_rename(const char *from,const char *to) {
    return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)?0:-1;
}
#define open fixture_open
#define pwrite fixture_pwrite
#define flock fixture_flock
#define rename fixture_rename
#define fsync _commit
#define ftruncate _chsize_s
#define close _close
#define unlink _unlink
''', encoding='ascii')
        fixture = OUT / 'check.cpp'
        fixture.write_text(r'''
#include <cassert>
#include <cstdlib>
#include "../../android/texconv/ut3tex.cpp"
extern "C" int texconv_convert_dds(const uint8_t *data,uint64_t size,int family,uint8_t **out,
    uint64_t *bytes,uint32_t *format,uint32_t *flags) {
    assert(bytes_total>0 && total>0);
    tvtx_dds d;assert(tvtx_dds_bytes(data,size,&d));
    *format=family==1?TVTX_GL_ETC2_RGB8:TVTX_GL_ASTC_4x4;*flags=0;*bytes=0;
    for(uint32_t l=0;l<d.mips;++l)*bytes+=tvtx_level_bytes(*format,std::max(1u,d.width>>l),std::max(1u,d.height>>l));
    *out=(uint8_t*)calloc(1,(size_t)*bytes);assert(*out);return 0;
}
extern "C" void texconv_free(void *p) { free(p); }
int main(int argc,char **argv) {
    assert(argc==8);std::string root=argv[1],index=argv[2];int fd=open(argv[3],O_RDONLY);assert(fd>=0);
    assert(!fs::exists(root));
    auto a=scan(root,index,fd);assert(a[0]==2&&a[1]==16&&a[2]==0&&!fs::exists(root));
    auto fast=quick_scan(root);assert(fast[0]==34833&&fast[1]==2879901632LL&&fast[2]==0&&!fs::exists(root));
    Game source(root,index,fd);auto paths=packages(source);assert(paths.size()==2);
    for(const auto &p:paths)assert(source.read(p).exports.size()==1);
    // The Windows ZIP fixture uses a seek; Android's real implementation uses thread-safe pread.
    for(int family:{1,2}) {
        total=bytes_total=0;
        assert(convert(root,family,1,index,fd)==0);
        assert(total==done && bytes_total==bytes_done);
        auto after=scan(root,index,fd);assert(after[0]==2&&after[2]==family&&after[3]==2&&after[4]>0);
        fast=quick_scan(root);assert(fast[2]==family&&fast[3]==after[3]&&fast[4]==after[4]);
        assert(!fs::exists(pack(root)+".part"));
    }
    auto previous=Package::read(pack(root));bool refused=false;
    std::ofstream(pack(root)+".part",std::ios::binary)<<"incomplete";
    assert(quick_scan(root)[2]==2);
    unlink((pack(root)+".part").c_str());
    for(size_t offset:{size_t(0),offsetof(tvtx_header,count),offsetof(tvtx_header,payload_bytes)}) {
        auto broken=previous;broken[offset]^=0x7f;
        std::ofstream(pack(root),std::ios::binary).write((const char*)broken.data(),broken.size());
        assert(quick_scan(root)[2]==0);
    }
    std::ofstream(pack(root),std::ios::binary).write((const char*)previous.data(),previous.size());
    assert(quick_scan(root)[2]==2);
    try{convert(root,1,1,argv[5],fd);}catch(const std::exception&){refused=true;}
    assert(refused&&Package::read(pack(root))==previous);close(fd);
    auto folder=scan(argv[4]);assert(folder[0]==2&&folder[1]==16);
    fd=open(argv[6],O_RDONLY);assert(fd>=0);std::string mixed=std::string(root)+"-mixed";
    Game combined(mixed,argv[7],fd);paths=packages(combined);assert(paths.size()==3);
    unsigned own=0;for(const auto &p:paths){own+=p.entry==nullptr;combined.read(p);}assert(own==2);
    auto b=scan(mixed,argv[7],fd);assert(b[0]==3&&b[1]==24);
    assert(convert(mixed,2,1,argv[7],fd)==0);close(fd);
    std::ofstream(mixed+"/UTGame/CookedPC/Characters/Test.upk",std::ios::binary)<<"broken";
    previous=Package::read(pack(mixed));fd=open(argv[6],O_RDONLY);assert(fd>=0);
    refused=false;try{convert(mixed,1,1,argv[7],fd);}catch(const std::exception&){refused=true;}
    assert(refused&&Package::read(pack(mixed))==previous&&!fs::exists(pack(mixed)+".part"));close(fd);
    refused=false;try{scan(root+"-absent");}catch(const std::exception &e){refused=std::string(e.what()).find("CookedPC")!=std::string::npos;}
    assert(refused);
    puts("PASS production texture source: ZIP with no sidecar, folder, stored offsets, compressed sidecar, case-insensitive overrides, both cache families, invalid index and failed replacement preserve complete cache");
}
''', encoding='ascii')
        env, linker = environment('x64')
        java = Path(os.environ['JAVA_HOME'])
        for source, obj, flags in ((fixture, OUT / 'check.obj', ['/std:c++17', '/EHsc']),
                                  (ROOT / 'android/launcher/native/zipmount.c', OUT / 'zipmount.obj', ['/std:c11'])):
            compiler = linker.with_name('cl.exe') if source.suffix == '.cpp' else CLANG
            target = [] if source.suffix == '.cpp' else ['/clang:--target=x86_64-pc-windows-msvc']
            result = subprocess.run([str(compiler), '/nologo', '/O2', '/D_CRT_SECURE_NO_WARNINGS', '/DNOMINMAX',
                *target, *flags, '/I'+str(OUT), '/I'+str(java/'include'),
                '/I'+str(java/'include/win32'), '/c', str(source), '/Fo'+str(obj)], cwd=ROOT,env=env,capture_output=True,text=True,timeout=90)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        result = subprocess.run([str(linker), '/nologo', str(OUT/'check.obj'), str(OUT/'zipmount.obj'),
            '/OUT:'+str(OUT/'check.exe')],env=env,capture_output=True,text=True,timeout=45)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        with tempfile.TemporaryDirectory(prefix='ut3-texture-source-') as temporary:
            folder = Path(temporary)
            stored, index = archive(folder)
            mixed, mixed_index = archive(folder,True)
            bad_index = folder/'invalid.zmi'
            bad_index.write_bytes(index.read_bytes()[:8]+struct.pack('<Q',stored.stat().st_size+1)+index.read_bytes()[16:])
            plain = folder/'plain/utgame/cookedpc/Characters'
            plain.mkdir(parents=True)
            (plain/'one.UPK').write_bytes(package(4));(plain/'two.u').write_bytes(package(5))
            sidecar = folder/'mount-ut3-game'
            overlay = Path(str(sidecar)+'-mixed')/'UTGame/CookedPC'
            (overlay/'Characters').mkdir(parents=True);(overlay/'Script').mkdir()
            (overlay/'Characters/test.UPK').write_bytes(package(6))
            (overlay/'Script/Compressed.u').write_bytes(package(3))
            result=subprocess.run([str(OUT/'check.exe'),str(sidecar),str(index),str(stored),str(folder/'plain'),
                str(bad_index),str(mixed),str(mixed_index)],capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            (OUT/'result.log').write_text(result.stdout+result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
