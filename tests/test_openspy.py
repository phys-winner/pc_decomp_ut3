"""Execute the production service patches against the pinned Steam PE image."""
import hashlib
import json
from pathlib import Path
import subprocess
import unittest

import pefile
from tests.host_environment import CLANG, environment

ROOT = Path(__file__).resolve().parents[1]


class OpenSpyTest(unittest.TestCase):
    def test_original_image_and_md5_boundary(self):
        out = ROOT / 'build/openspy-tests'
        out.mkdir(parents=True, exist_ok=True)
        config = json.loads((ROOT / 'config/inputs.json').read_text())
        original = Path(config['source_root']) / 'Binaries/UT3.exe'
        expected = next(x['sha256'] for x in config['modules'] if x['path'] == 'Binaries/UT3.exe')
        self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), expected)
        pe = pefile.PE(str(original), fast_load=True)
        self.assertEqual(pe.OPTIONAL_HEADER.ImageBase, 0x400000)
        (out / 'image.bin').write_bytes(pe.get_memory_mapped_image())
        fixture = out / 'check.c'
        fixture.write_text(r'''
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VOID_HOST_H
static uint8_t *memory;
static uint32_t g_eax,g_esp;
#define GPTR(p) (memory+(uint32_t)(p))
#define MEM8(p) (*(uint8_t *)GPTR(p))
#define MEM32(p) (*(uint32_t *)GPTR(p))
#define ARG(i) MEM32(g_esp+4+4*(i))
static unsigned protections;
static jmp_buf trap;
static int void_vm_readable(uint32_t p,uint32_t n){return p>=0x400000&&n<=0x2000000&&p<=0x2400000-n;}
static int void_vm_writable(uint32_t p,uint32_t n){return void_vm_readable(p,n);}
static int void_vm_protect(uint32_t p,uint32_t n,uint32_t mode,uint32_t *old){assert(void_vm_readable(p,n));*old=mode==4?2:4;++protections;return 1;}
static void void_log(const char *format,...){(void)format;}
static void void_lift_trap(uint32_t address,const char *reason){assert(address==0x01243A50);assert(reason);longjmp(trap,1);}
#include "../../android/host/openspy.c"
int main(int argc,char **argv){
    assert(argc==2);memory=calloc(1,0x2400000);assert(memory);
    FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(memory+0x400000,1,0x2000000,f)>0);fclose(f);
    unsigned count=sizeof patches/sizeof *patches;
    assert(count==14);
    for(unsigned i=0;i<count;i++){
        const openspy_patch *p=patches+i;
        assert(!strcmp((char *)GPTR(p->address),p->before));
        /* Longer endpoint names must consume padding, never the next string. */
        for(size_t j=strlen(p->before)+1;j<strlen(p->after)+1;j++)assert(MEM8(p->address+j)==0);
    }
    uint32_t bad=patches[count-1].address;MEM8(bad)^=1;
    assert(!void_openspy_init()&&protections==0);
    assert(!strcmp((char *)GPTR(patches[0].address),patches[0].before));
    MEM8(bad)^=1;assert(void_openspy_init());assert(protections==count*2);
    for(unsigned i=0;i<count;i++)assert(!strcmp((char *)GPTR(patches[i].address),patches[i].after));
    g_esp=0x2300000;MEM32(g_esp)=0x12345678;MEM32(g_esp+4)=0x2300100;MEM32(g_esp+8)=0x2300200;
    for(unsigned i=0;i<16;i++)MEM8(0x2300100+i)=i*17;
    MEM8(0x2300200+33)=0xAD;void_openspy_md5();
    assert(g_eax==16&&g_esp==0x2300004&&MEM32(0x2300000)==0x12345678);
    assert(!strcmp((char *)GPTR(0x2300200),"00112233445566778899aabbccddeeff"));assert(MEM8(0x2300200+33)==0xAD);
    /* In-place conversion must retain all sixteen input bytes. */
    g_esp=0x2300000;MEM32(g_esp+8)=0x2300100;void_openspy_md5();
    assert(!strcmp((char *)GPTR(0x2300100),"00112233445566778899aabbccddeeff"));
    g_esp=0x2300000;MEM32(g_esp+4)=0x23FFFF8;
    if(!setjmp(trap)){void_openspy_md5();assert(!"bad source accepted");}
    MEM32(g_esp+4)=0x2300100;MEM32(g_esp+8)=0x23FFFF0;
    if(!setjmp(trap)){void_openspy_md5();assert(!"bad destination accepted");}
    free(memory);puts("PASS production OpenSpy: 14 Steam-image patches/padding, fail-before-write validation, protection restoration, lowercase MD5/cdecl/alias/buffer bounds");return 0;
}
''', encoding='utf-8')
        env, linker = environment('x64')
        for command in ([str(CLANG), '/nologo', '/O2', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                         '/clang:--target=x86_64-pc-windows-msvc', '/c', str(fixture), '/Fo' + str(out / 'check.obj')],
                        [str(linker), '/nologo', str(out / 'check.obj'), '/OUT:' + str(out / 'check.exe')],
                        [str(out / 'check.exe'), str(out / 'image.bin')]):
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=45)
            self.assertEqual(result.returncode, 0, (result.stdout + result.stderr).decode(errors='replace'))
        (out / 'result.log').write_bytes(result.stdout + result.stderr)
        print(result.stdout.decode().strip())


if __name__ == '__main__':
    unittest.main()
