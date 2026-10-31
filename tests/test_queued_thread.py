"""Read-only FQueuedThread audit: original x86 versus current generated ARM64."""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

import capstone
from elftools.elf.elffile import ELFFile
import pefile
from unicorn import Uc, UC_ARCH_ARM64, UC_ARCH_X86, UC_MODE_ARM, UC_MODE_32, UC_HOOK_CODE
from unicorn import arm64_const as arm, x86_const as x86

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x2000000000
STACK, WORKER, EVENT, CS, JOB, POOL, FACTORY, TABLES, TRACE = [0x308000, 0x320000, 0x321000, 0x322000, 0x323000, 0x324000, 0x325000, 0x326000, 0x328000]
STOP, STUB = 0x200000, 0x201000
REGS = ['eax','ebx','ecx','edx','esi','edi','ebp','esp']
XR = [getattr(x86, 'UC_X86_REG_'+s.upper()) for s in REGS]
IAT = {0x1729140:1, 0x172913C:2, 0x1729144:3, 0x17292E0:10, 0x17290FC:11, 0x1729154:12}
ARGC = {0:1,1:1,2:1,3:1,4:0,5:0,6:1,7:0,8:2,9:0,10:6,11:2,12:1,13:1,14:2,15:4}
OWNERS = [0xBA6CD0,0xBA6DD0,0xBA7100,0xBA7140]
RANGES = [(0xBA6CD0,0xBA6DC5),(0xBA6DD0,0xBA6EAF),(0xBA7100,0xBA7132),(0xBA7140,0xBA7245),(0xBA7250,0xBA7311)]
INITIAL = [0xA1234567,0xB1234567,0xC1234567,0xD1234567,0x51234567,0x61234567,0x71234567,STACK]

HARNESS = r'''
#include "recomp_types.h"
uint32_t g_eax,g_ebx,g_ecx,g_edx,g_esi,g_edi,g_ebp,g_esp;
uint32_t g_flag_k,g_flag_a,g_flag_b,g_flag_cf,g_fs_base=0x380000;
uint32_t g_backedges,fixture_failure;
void (*recomp_yield_hook)(void);
#define T 0x328000u
#define W 0x320000u
#define E 0x321000u
#define C 0x322000u
#define J 0x323000u
#define P 0x324000u
#define F 0x325000u
static void invoke(unsigned tag,unsigned argc) {
 uint32_t n=MEM32(T),r=T+64+n*40;MEM32(T)=n+1;
 if(n>=64){fixture_failure=1;for(;;){}}
 MEM32(r)=tag;MEM32(r+4)=g_ecx;
 for(unsigned i=0;i<6;i++) MEM32(r+8+4*i)=i<argc?MEM32(g_esp+4+4*i):0;
 MEM32(r+32)=MEM32(W+0x14);MEM32(r+36)=MEM32(T+4);
 uint32_t result=0;
 switch(tag) {
 case 0:MEM32(T+24)=0;result=1;break;
 case 1:result=MEM32(T+8)?0:1;if(result)MEM32(T+4)++;break;
 case 2:MEM32(T+4)++;break;
 case 3:if(MEM32(T+4)!=1)fixture_failure=2;MEM32(T+4)--;break;
 case 4:if(g_ecx!=J||MEM32(T+4)!=1||MEM32(W+0x14)!=J)fixture_failure=3;MEM32(J+4)++;break;
 case 5:if(g_ecx!=J||MEM32(T+4)!=1||MEM32(W+0x14)!=J)fixture_failure=4;MEM32(J+8)++;break;
 case 6:if(g_ecx!=P||MEM32(T+4)||MEM32(W+0x14))fixture_failure=5;MEM32(W+0x10)=1;break;
 case 7:if(g_ecx!=E||MEM32(T+4))fixture_failure=6;MEM32(T+24)=1;break;
 case 8:result=E;break;
 case 9:result=MEM32(T+16)?0:C;break;
 case 10:if(!MEM32(T+12)){MEM32(MEM32(g_esp+24))=0x108;result=0x1014;}break;
 case 11:result=1;break;
 case 15:MEM32(T+20)++;break;
 }
 g_eax=result;g_esp+=4+(tag==14||tag==15?0:argc*4);
}
#define S(n,a) static void stub##n(void){invoke(n,a);}
S(0,1) S(1,1) S(2,1) S(3,1) S(4,0) S(5,0) S(6,1) S(7,0) S(8,2) S(9,0) S(10,6) S(11,2) S(12,1) S(13,1)
void sub_00BA6880(void){invoke(14,2);}
void sub_00B55380(void){invoke(15,4);}
static void bad(void){fixture_failure=0xBAD;for(;;){}}
recomp_func_t void_resolve(uint32_t va){switch(va){
#define K(n) case 0x201000u+16*n:return stub##n;
K(0) K(1) K(2) K(3) K(4) K(5) K(6) K(7) K(8) K(9) K(10) K(11) K(12) K(13)
 default:return bad;}}
void void_lift_trap(uint32_t va,const char *why){fixture_failure=va;for(;;){}}
'''


def read32(vm, address):
    return struct.unpack('<I',vm.mem_read(address,4))[0]


def write32(vm,address,value):
    vm.mem_write(address,struct.pack('<I',value&0xFFFFFFFF))


def initialize(vm,pe,base,case):
    vm.mem_map(base+0x300000,0x40000)
    vm.mem_map(base+0x380000,0x10000)
    write32(vm,base+0x380000,0x11223344)
    write32(vm,base+WORKER,TABLES)
    write32(vm,base+WORKER+4,EVENT)
    write32(vm,base+WORKER+0x18,CS)
    write32(vm,base+WORKER+0x1c,POOL)
    write32(vm,base+WORKER+0x14,0 if case.get('empty') else JOB)
    write32(vm,base+WORKER+0x10,int(case.get('stopped',False)))
    for offset,key in [(8,'try_fail'),(12,'thread_fail'),(16,'cs_fail')]:write32(vm,base+TRACE+offset,int(case.get(key,False)))
    write32(vm,base+TRACE+24,1)
    for obj,table in [(EVENT,TABLES+0x40),(JOB,TABLES+0x80),(POOL,TABLES+0xc0),(FACTORY,TABLES+0x100)]:write32(vm,base+obj,table)
    for offset,tag in [(0x40+0x14,0),(0x40+8,7),(0x80+4,4),(0x80+12,5),(0xc0+16,6),(0x100,9),(0x104,8),(0x108,13)]:write32(vm,base+TABLES+offset,STUB+tag*16)
    for address,tag in IAT.items():write32(vm,base+address,STUB+tag*16)
    write32(vm,base+0x1DE6154,FACTORY)
    owner=case['owner']
    if owner==0xBA7100:args=[WORKER]
    elif owner==0xBA6DD0:args=[JOB];write32(vm,base+WORKER+0x14,0)
    elif owner==0xBA7140:
        args=[POOL,case.get('priority',0),0x8000]
        for off in range(4,0x20,4):write32(vm,base+WORKER+off,0)
    else:args=[]
    vm.mem_write(base+STACK,struct.pack('<'+'I'*(len(args)+1),STOP,*args))
    regs=INITIAL.copy()
    if owner!=0xBA7100:regs[2]=WORKER
    return regs


def result(vm,base,registers,fs):
    trace=bytes(vm.mem_read(base+TRACE,64+read32(vm,base+TRACE)*40))
    return {'registers':registers,'worker':bytes(vm.mem_read(base+WORKER,32)).hex(),
            'job':bytes(vm.mem_read(base+JOB,12)).hex(),'trace':trace.hex(),'fs0':fs}


def original(pe,case):
    vm=Uc(UC_ARCH_X86,UC_MODE_32)
    vm.mem_map(0x400000,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095)
    vm.mem_write(0x400000,pe.get_memory_mapped_image())
    vm.mem_map(STOP,0x2000);vm.mem_map(0,0x1000);write32(vm,0,0x11223344)
    regs=initialize(vm,pe,0,case)
    for reg,value in zip(XR,regs):vm.reg_write(reg,value)
    vm.reg_write(x86.UC_X86_REG_EFLAGS,0x246)
    def hook(machine,address,size,_):
        tag=(address-STUB)//16 if STUB<=address<STUB+14*16 and not (address-STUB)%16 else 14 if address==0xBA6880 else 15 if address==0xB55380 else None
        if tag is None:return
        sp=machine.reg_read(x86.UC_X86_REG_ESP);ecx=machine.reg_read(x86.UC_X86_REG_ECX);argc=ARGC[tag]
        argv=[read32(machine,sp+4+i*4) for i in range(argc)]
        n=read32(machine,TRACE);write32(machine,TRACE,n+1)
        row=[tag,ecx,*argv[:6],*([0]*(6-min(argc,6))),read32(machine,WORKER+0x14),read32(machine,TRACE+4)]
        machine.mem_write(TRACE+64+n*40,struct.pack('<10I',*row))
        ret=0;depth=read32(machine,TRACE+4)
        if tag==0:write32(machine,TRACE+24,0);ret=1
        elif tag==1:
            ret=0 if read32(machine,TRACE+8) else 1
            if ret:write32(machine,TRACE+4,depth+1)
        elif tag==2:write32(machine,TRACE+4,depth+1)
        elif tag==3:assert depth==1;write32(machine,TRACE+4,depth-1)
        elif tag in (4,5):
            assert ecx==JOB and depth==1 and read32(machine,WORKER+0x14)==JOB
            off=4 if tag==4 else 8;write32(machine,JOB+off,read32(machine,JOB+off)+1)
        elif tag==6:
            assert ecx==POOL and depth==0 and read32(machine,WORKER+0x14)==0
            write32(machine,WORKER+0x10,1)
        elif tag==7:assert ecx==EVENT and depth==0;write32(machine,TRACE+24,1)
        elif tag==8:ret=EVENT
        elif tag==9:ret=0 if read32(machine,TRACE+16) else CS
        elif tag==10:
            if not read32(machine,TRACE+12):write32(machine,argv[5],0x108);ret=0x1014
        elif tag==11:ret=1
        elif tag==15:write32(machine,TRACE+20,read32(machine,TRACE+20)+1)
        machine.reg_write(x86.UC_X86_REG_EAX,ret)
        machine.reg_write(x86.UC_X86_REG_ESP,sp+4+(0 if tag in (14,15) else argc*4))
        machine.reg_write(x86.UC_X86_REG_EIP,read32(machine,sp))
    vm.hook_add(UC_HOOK_CODE,hook)
    vm.emu_start(case['owner'],STOP,count=10000)
    assert vm.reg_read(x86.UC_X86_REG_EIP)==STOP
    return result(vm,0,[vm.reg_read(reg) for reg in XR],read32(vm,0))


def compile_generated(out):
    source=(ROOT/'generated/ut3/functions_0273.c').read_text()
    bodies=[re.search(r'void sub_%08X\(void\) \{.*?(?=\nvoid sub_|\Z)'%a,source,re.S)[0] for a in OWNERS]
    text='#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n'+ '\n'.join('void sub_%08X(void);'%a for a in [*OWNERS,0xBA6880,0xB55380])+'\n'+'\n'.join(bodies)
    (out/'generated.c').write_text(text);(out/'harness.c').write_text(HARNESS)
    clang=Path.home()/'AppData/Local/Android/Sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    flags=[str(clang),'--target=aarch64-linux-android29','-O2','-fno-strict-aliasing','-fwrapv','-ffp-contract=off','-fno-math-errno','-ffreestanding','-fno-builtin','-DRECOMP_X87_WINDOW','-I'+str(ROOT/'android/runtime')]
    commands=[]
    command=flags+['-nostdlib','-static','-Wl,-e,sub_00BA7100','-Wl,-Ttext=0x40000000',str(out/'harness.c'),str(out/'generated.c'),'-o',str(out/'plain')]
    subprocess.run(command,check=True,capture_output=True);commands.append(command)
    for name,extra in [('harness',[]),('generated',['-DRECOMP_GUEST_STATE_SEPARATE','-mllvm','-basic-aa-separate-storage','-include',str(ROOT/'android/runtime/guest_state_prefix.h')])]:
        command=flags+extra+['-c',str(out/(name+'.c')),'-o',str(out/(name+'.o'))]
        subprocess.run(command,check=True,capture_output=True);commands.append(command)
    command=[str(clang),'--target=aarch64-linux-android29','-nostdlib','-static','-Wl,-e,sub_00BA7100','-Wl,-Ttext=0x40000000',str(out/'harness.o'),str(out/'generated.o'),'-o',str(out/'separate')]
    subprocess.run(command,check=True,capture_output=True);commands.append(command)
    return commands


def compiled(binary,pe,case):
    vm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);mapped=set()
    with binary.open('rb') as stream:
        elf=ELFFile(stream)
        for segment in elf.iter_segments():
            if segment['p_type']!='PT_LOAD':continue
            for address in range(segment['p_vaddr']&~4095,(segment['p_vaddr']+segment['p_memsz']+4095)&~4095,4096):
                if address not in mapped:vm.mem_map(address,4096);mapped.add(address)
            vm.mem_write(segment['p_vaddr'],segment.data())
        symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    vm.mem_map(BASE+0x400000,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095)
    vm.mem_write(BASE+0x400000,pe.get_memory_mapped_image())
    regs=initialize(vm,pe,BASE,case)
    for name,value in zip(REGS,regs):write32(vm,symbols['g_'+name],value)
    vm.mem_map(0x7100000000,0x10000);vm.mem_map(0x7200000000,0x1000)
    vm.reg_write(arm.UC_ARM64_REG_SP,0x710000f000);vm.reg_write(arm.UC_ARM64_REG_LR,0x7200000000)
    vm.emu_start(symbols['sub_%08X'%case['owner']],0x7200000000,count=100000)
    assert vm.reg_read(arm.UC_ARM64_REG_PC)==0x7200000000
    assert read32(vm,symbols['fixture_failure'])==0
    return result(vm,BASE,[read32(vm,symbols['g_'+name]) for name in REGS],read32(vm,BASE+0x380000))


def main():
    inputs=json.loads((ROOT/'config/inputs.json').read_text());row=next(r for r in inputs['modules'] if r['path']=='Binaries/UT3.exe')
    raw=(Path(inputs['source_root'])/row['path']).read_bytes();assert hashlib.sha256(raw).hexdigest()==row['sha256']
    pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    disassembly=[]
    for lo,hi in RANGES:
        instructions=list(md.disasm(pe.get_data(lo-0x400000,hi-lo),lo))
        disassembly.append({'start':hex(lo),'end':hex(hi),'instructions':[f'{i.address:08X} {i.bytes.hex():<24} {i.mnemonic} {i.op_str}' for i in instructions],
            'atomic_instructions':[hex(i.address) for i in instructions if 'xchg' in i.mnemonic or i.mnemonic.startswith('lock')]})
    assert all(not item['atomic_instructions'] for item in disassembly)
    out=ROOT/'build/queued-thread-audit';out.mkdir(parents=True,exist_ok=True);commands=compile_generated(out)
    cases=[{'name':'stdcall-start-job','owner':0xBA7100},{'name':'run-direct-job','owner':0xBA6CD0},
           {'name':'contended-cs-job','owner':0xBA6CD0,'try_fail':True},
           {'name':'spurious-wake-empty-job','owner':0xBA6CD0,'empty':True},
           {'name':'already-stopped','owner':0xBA6CD0,'stopped':True},
           {'name':'assign-work','owner':0xBA6DD0},{'name':'assign-contended','owner':0xBA6DD0,'try_fail':True},
           {'name':'create-success-default-priority','owner':0xBA7140,'empty':True},
           {'name':'create-success-negative-priority','owner':0xBA7140,'priority':0xffffffff,'empty':True},
           {'name':'create-cs-failure','owner':0xBA7140,'cs_fail':True,'empty':True},
           {'name':'create-thread-failure','owner':0xBA7140,'thread_fail':True,'empty':True}]
    results=[]
    for case in cases:
        expected=original(pe,case)
        for variant in ('plain','separate'):
            actual=compiled(out/variant,pe,case)
            assert actual==expected,(case['name'],variant,actual,expected)
            assert expected['fs0']==0x11223344
            results.append({'case':case,'variant':variant,'trace_events':int.from_bytes(bytes.fromhex(actual['trace'])[:4],'little'),'result':actual})
    source_pins={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__),ROOT/'generated/ut3/functions_0273.c',ROOT/'android/runtime/recomp_types.h',ROOT/'android/runtime/guest_state_prefix.h',ROOT/'android/host/threads.c']}
    report={'original_sha256':row['sha256'],'read_only':True,'device_used':False,'cases':results,'comparisons':len(results),'disassembly':disassembly,'commands':commands,'source_pins':source_pins,'result':'PASS','scope':'Original x86 bytes versus extracted current generated C compiled O2 ARM64, production header and generated-only state separation. Synchronization/event/job callees are explicit deterministic fixtures; native scheduling is not simulated.'}
    (ROOT/'research/queued-thread-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS original x86 versus current generated O2 ARM64: '+str(len(results))+' comparisons; Run/Assign/Create/start ABI, CS ordering, slot+14 lifetime, stop+10, cleanup; no XCHG in work-slot path')


if __name__=='__main__':main()
