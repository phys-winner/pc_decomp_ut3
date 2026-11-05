"""Compare the production x87 lift with the pinned PhysX instruction on x86."""
import math
from pathlib import Path
import struct
import subprocess
import sys
import unittest

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EFLAGS
from tests.host_environment import CLANG, environment
from tests import test_touch_controls as touch_checks

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from lift_game import CheckedLifter


class FsincosTest(unittest.TestCase):
    def test_cosine_on_top_sine_below_and_lower_stack_preserved(self):
        pe = pefile.PE(str(ROOT/'generated/input/PhysXCore.dll'), fast_load=True)
        original = pe.get_data(0x1E01D674-pe.OPTIONAL_HEADER.ImageBase, 2)
        self.assertEqual(original, bytes.fromhex('d9fb'))
        decoder = Cs(CS_ARCH_X86, CS_MODE_32)
        decoder.detail = True
        lifted = '\n'.join(CheckedLifter().lift_instruction(next(decoder.disasm(original, 0x1E01D674))))
        header = (ROOT/'android/runtime/recomp_types.h').read_text(encoding='utf-8')
        push = touch_checks.TouchControlsTest._method(header, 'static inline void fp_push_impl(')
        angles = (0.0, 1e-6, -1e-6, .01, -.01, math.pi/180, -math.pi/180,
                  math.pi/4, -math.pi/4, math.pi/2, math.pi, 2*math.pi, 10.0, -10.0)
        out = ROOT/'build/fsincos-test'
        out.mkdir(exist_ok=True)
        source = out/'check.c'
        source.write_text('#include <assert.h>\n#include <math.h>\n#include <stdio.h>\n#include <stdint.h>\nint64_t g_st_i64[8];\n' + push + '\n' +
            '#define fp_push(v) fp_push_impl(_st,&_fp_top,(v))\nint main(void){\n' +
            'double angles[]={' + ','.join(repr(a) for a in angles) + '};\n' +
            'for(unsigned i=0;i<sizeof angles/sizeof *angles;++i){double _st[8]={angles[i],9.125};int _fp_top=1;int _fpu_cmp=0;\n' +
            lifted + '\nassert(_fp_top==2 && _st[2]==9.125);printf("%.17g %.17g\\n",_st[0],_st[1]);}\nreturn 0;}\n')
        env, linker = environment('x64')
        commands = ([str(CLANG), '/nologo','/O2','/std:c11','/clang:--target=x86_64-pc-windows-msvc',
                     '/c',str(source),'/Fo'+str(out/'check.obj')],
                    [str(linker),'/nologo',str(out/'check.obj'),'/OUT:'+str(out/'check.exe')],
                    [str(out/'check.exe')])
        for command in commands:
            result = subprocess.run(command,env=env,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        actual = [tuple(map(float,line.split())) for line in result.stdout.splitlines()]
        for angle, pair in zip(angles, actual, strict=True):
            uc = Uc(UC_ARCH_X86, UC_MODE_32)
            uc.mem_map(0x10000,4096)
            # FNINIT, FLD sentinel, FLD angle, original FSINCOS, FSTP cos/sin/sentinel.
            code = bytes.fromhex('dbe3dd05')+struct.pack('<I',0x10308)+bytes.fromhex('dd05')+struct.pack('<I',0x10300)+original
            for address in (0x10400,0x10408,0x10410):
                code += bytes.fromhex('dd1d')+struct.pack('<I',address)
            uc.mem_write(0x10000,code)
            uc.mem_write(0x10300,struct.pack('<dd',angle,9.125))
            uc.reg_write(UC_X86_REG_EFLAGS,0x202)
            uc.emu_start(0x10000,0x10000+len(code),count=20)
            cosine,sine,sentinel = struct.unpack('<ddd',uc.mem_read(0x10400,24))
            self.assertEqual(sentinel,9.125)
            self.assertEqual(uc.reg_read(UC_X86_REG_EFLAGS),0x202)
            self.assertAlmostEqual(cosine,math.cos(angle),places=12)
            self.assertAlmostEqual(sine,math.sin(angle),places=12)
            self.assertAlmostEqual(pair[0],cosine,places=12,msg=f'cosine ST(0), angle {angle}')
            self.assertAlmostEqual(pair[1],sine,places=12,msg=f'sine ST(1), angle {angle}')


if __name__ == '__main__':
    unittest.main()
