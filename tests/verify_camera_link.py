"""Verify real AOT relocations resolve through both Hor+ wrappers in an ELF."""
import argparse
import hashlib
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile


def edges(path, target):
    with path.open('rb') as stream:
        elf=ELFFile(stream);symbols=elf.get_section_by_name('.symtab')
        functions=[s for s in symbols.iter_symbols() if s['st_info']['type']=='STT_FUNC' and s['st_size']]
        result=[]
        for section in elf.iter_sections():
            if section['sh_type']!='SHT_RELA':continue
            for relocation in section.iter_relocations():
                if symbols.get_symbol(relocation['r_info_sym']).name!=target:continue
                offset=relocation['r_offset']
                owners=[s for s in functions if s['st_shndx']==section['sh_info'] and s['st_value']<=offset<s['st_value']+s['st_size']]
                assert len(owners)==1
                result.append((owners[0].name,offset-owners[0]['st_value'],relocation['r_info_type']))
        return result


def verify(path, objects, wrapper):
    with path.open('rb') as stream:
        elf=ELFFile(stream)
        symbols={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
        def word(address,size=4):
            for segment in elf.iter_segments():
                if segment['p_type']=='PT_LOAD' and segment['p_vaddr']<=address<segment['p_vaddr']+segment['p_filesz']:
                    stream.seek(segment['p_offset']+address-segment['p_vaddr']);return int.from_bytes(stream.read(size),'little')
            raise AssertionError('No linked file-backed address')
        def branch(owner,offset,kind,target):
            address=symbols[owner]['st_value']+offset;instruction=word(address)
            assert kind in (282,283)
            assert instruction&0xFC000000==(0x14000000 if kind==282 else 0x94000000)
            imm=instruction&0x3FFFFFF
            if imm&0x2000000:imm-=0x4000000
            actual=address+imm*4
            # Real final DSOs can require an LLD long-range thunk.
            direct=actual
            for _ in range(2):
                if actual==target:break
                thunk=next((s for s in symbols.values() if s['st_value']==actual and 'Thunk_' in s.name),None)
                assert thunk is not None,(owner,hex(address),hex(actual),hex(target))
                # LLD AArch64 ADRP/ADD/BR thunk.
                a,b,c=word(actual),word(actual+4),word(actual+8)
                assert a&0x9F00001F==0x90000010 and b&0xFFC003FF==0x91000210 and c==0xD61F0200
                delta=((a>>29)&3)|(((a>>5)&0x7FFFF)<<2)
                if delta&0x100000:delta-=0x200000
                actual=(actual&~4095)+(delta<<12)+((b>>10)&4095)
            assert actual==target,(owner,hex(actual),hex(target))
            return {'owner':owner,'offset':hex(offset),'instruction':hex(address),'direct_target':hex(direct),'target':hex(actual)}
        def dispatch(target,wrapped,guest):
            with (objects/'generated/dispatch.o').open('rb') as f:
                obj=ELFFile(f);syms=obj.get_section_by_name('.symtab')
                table=next(s for s in syms.iter_symbols() if s.name=='recomp_dispatch_table')
                matches=[]
                for section in obj.iter_sections():
                    if section['sh_type']!='SHT_RELA' or section['sh_info']!=table['st_shndx']:continue
                    for r in section.iter_relocations():
                        if syms.get_symbol(r['r_info_sym']).name==target:
                            assert r['r_info_type']==257;matches.append(r['r_offset']-table['st_value'])
            assert len(matches)==1
            address=symbols['recomp_dispatch_table']['st_value']+matches[0]
            relocs=[r for r in elf.get_section_by_name('.rela.dyn').iter_relocations() if r['r_offset']==address]
            assert len(relocs)==1 and relocs[0]['r_info_type']==1027 and relocs[0]['r_addend']==wrapped
            assert word(address-8)==guest
            return {'guest':hex(guest),'target':hex(wrapped)}
        report={}
        for target,files,count,guest in [
            ('sub_00486270',['functions_0312.o'],2,0x486270),
            ('sub_00C85710',['functions_0095.o','functions_0312.o','functions_0337.o'],4,0xC85710)]:
            wrapped=symbols['__wrap_'+target]['st_value'];real=symbols[target]['st_value'];assert wrapped!=real
            callers=[]
            for filename in files:
                for owner,offset,kind in edges(objects/'generated/UT3'/filename,target):
                    callers.append(branch(owner,offset,kind,wrapped))
            assert len(callers)==count,(target,len(callers),count)
            real_calls=[branch(owner,offset,kind,real) for owner,offset,kind in edges(wrapper,'__real_'+target)]
            assert real_calls
            report[target]={'callers':callers,'wrapper_to_real':real_calls,'dispatch':dispatch(target,wrapped,guest)}
        return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--objects',type=Path,required=True)
    parser.add_argument('--wrapper-object',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    report={'status':'PASS_REAL_AOT_ELF_BOTH_CAMERA_WRAPPERS','elf_sha256':hashlib.sha256(args.elf.read_bytes()).hexdigest(),
            'edges':verify(args.elf,args.objects,args.wrapper_object),'device_executed':False}
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS real AOT ELF:2 ctor +4 CalcSceneView calls +2 dispatch pointers; both wrapper->real edges')


if __name__=='__main__':main()
