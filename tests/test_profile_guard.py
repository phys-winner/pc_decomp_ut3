"""Pinned original profile-writer entry and the native sign-in boundary."""
from pathlib import Path
import subprocess
import unittest
import pefile
from tests.host_environment import CLANG, environment

ROOT = Path(__file__).resolve().parents[1]


class ProfileGuardTest(unittest.TestCase):
    def test_write_requires_local_or_online_signin(self):
        pe = pefile.PE(str(ROOT / 'generated/input/UT3.exe'))
        entry = pe.get_data(0x0120A900 - pe.OPTIONAL_HEADER.ImageBase, 10)
        self.assertEqual(entry, bytes.fromhex('558bec6aff6823ff6b01'))
        out = ROOT / 'build/profile-guard-test'
        out.mkdir(parents=True, exist_ok=True)
        (out / 'check.c').write_text(r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define VOID_HOST_H
typedef void (*void_fn)(void);
static uint8_t *memory;
static uint32_t g_ecx,g_eax,g_esp;
static int calls,installed,missing,denied;
static void_fn writer;
#define GPTR(p) (memory+(uint32_t)(p))
#define MEM8(p) (*(uint8_t*)GPTR(p))
static int void_vm_readable(uint32_t p,uint32_t size){return p&&p<0x2000000&&size<=0x2000000-p&&p!=(uint32_t)denied;}
static void original(void){++calls;g_eax=1;g_esp+=12;}
static void_fn void_dispatch_find(uint32_t address){assert(address==0x120A900);return missing?NULL:original;}
static void void_dispatch_add(uint32_t address,void_fn fn){assert(address==0x120A900&&fn);writer=fn;++installed;}
static void void_log(const char *format,...){(void)format;}
#include "../../android/host/profiles.c"
int main(void){
    memory=calloc(1,0x2000000);assert(memory);
    assert(!void_profiles_init()&&!installed&&!profile_write_original);
    const uint8_t entry[]={0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x23,0xFF,0x6B,0x01};
    memcpy(GPTR(PROFILE_WRITE),entry,sizeof entry);missing=1;assert(!void_profiles_init()&&!installed);
    missing=0;assert(void_profiles_init()&&installed==1&&writer==profile_write);
    assert(void_profiles_init()&&installed==1);
    for(unsigned status=0;status<3;++status){
        g_ecx=0x100000;g_esp=0x2000;g_eax=0xAA;MEM8(g_ecx+PROFILE_LOGIN_STATUS)=(uint8_t)status;
        int before=calls;writer();
        assert(g_esp==0x200C&&g_ecx==0x100000&&g_eax==(status?1u:0u));
        assert(calls==before+(status!=0)&&MEM8(g_ecx+PROFILE_LOGIN_STATUS)==status);
    }
    denied=0x100000+PROFILE_LOGIN_STATUS;g_esp=0x2000;int before=calls;writer();
    assert(calls==before+1&&g_esp==0x200C);
    puts("PASS profile writer: pinned entry validation/idempotence, signed-out refusal, local/online original calls, unreadable-state fallback, thiscall stack and unchanged subsystem state");
    free(memory);return 0;
}
''', encoding='ascii')
        env, linker = environment('x64')
        for command in (
            [CLANG, '/nologo', '/O2', '/std:c11', '/clang:--target=x86_64-pc-windows-msvc', '/c', out/'check.c', '/Fo'+str(out/'check.obj')],
            [linker, '/nologo', out/'check.obj', '/OUT:'+str(out/'check.exe')], [out/'check.exe']):
            result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(result.stdout.strip())
