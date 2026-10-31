"""Read-only original UT3 native draw arithmetic; no initialized game/window."""
import hashlib
import json
from pathlib import Path
import struct

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_EIP

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/hud-aspect-probe'
OUT.mkdir(parents=True, exist_ok=True)
cfg = json.loads((ROOT / 'config/inputs.json').read_text())
exe = Path(cfg['source_root']) / 'Binaries/UT3.exe'
expected = next(x['sha256'] for x in cfg['modules'] if x['path'] == 'Binaries/UT3.exe')
assert hashlib.sha256(exe.read_bytes()).hexdigest() == expected
pe = pefile.PE(str(exe), fast_load=True)
image = pe.get_memory_mapped_image()
BASE, HEAP, STACK, STOP = 0x400000, 0x20000000, 0x30010000, 0x100000
HUD, CANVAS, WORLD, STRING, TEXT = [HEAP + x for x in (0, 0x2000, 0x4000, 0x6000, 0x7000)]


class Original:
    def __init__(self):
        self.vm = Uc(UC_ARCH_X86, UC_MODE_32)
        self.vm.mem_map(BASE, (len(image) + 4095) & ~4095)
        self.vm.mem_write(BASE, image)
        self.vm.mem_map(HEAP, 0x10000)
        self.vm.mem_map(0x30000000, 0x20000)
        self.vm.mem_map(STOP, 4096)
        self.vm.reg_write(UC_X86_REG_ESP, STACK)
        self.draws = []
        self.vm.hook_add(UC_HOOK_CODE, self.step)

    def word(self, p):
        return struct.unpack('<I', self.vm.mem_read(p, 4))[0]

    def words(self, p, *values):
        self.vm.mem_write(p, struct.pack('<'+'I'*len(values), *values))

    def number(self, p):
        return struct.unpack('<f', self.vm.mem_read(p, 4))[0]

    def floats(self, p, *values):
        self.vm.mem_write(p, struct.pack('<'+'f'*len(values), *values))

    def finish(self, popped):
        sp = self.vm.reg_read(UC_X86_REG_ESP)
        self.vm.reg_write(UC_X86_REG_EIP, self.word(sp))
        self.vm.reg_write(UC_X86_REG_ESP, sp+4+popped)

    def step(self, vm, pc, size, _):
        sp = vm.reg_read(UC_X86_REG_ESP)
        if pc == 0xEF9060:  # External font measurement boundary only.
            assert self.word(sp+4) == CANVAS
            assert self.number(sp+12) == self.number(sp+16) == 1
            assert self.word(sp+28) == TEXT
            self.words(self.word(sp+20), 100)
            self.words(self.word(sp+24), 20)
            self.finish(0)
        elif pc == 0xEF69F0:  # Final native Canvas text submission boundary.
            assert vm.reg_read(UC_X86_REG_ECX) == CANVAS
            assert self.word(sp+32) == TEXT
            self.draws.append({'scale_x': self.number(sp+20), 'scale_y': self.number(sp+24),
                               'x': self.number(CANVAS+0x50), 'y': self.number(CANVAS+0x54)})
            self.finish(32)


vtable = 0x17CD310
assert image[0x6167C5-BASE:0x6167CB-BASE] == b'\xc7\x06\x10\xd3\x7c\x01'
assert struct.unpack_from('<I', image, vtable+0x308-BASE)[0] == 0x6A51A0
assert struct.unpack_from('<I', image, vtable+0x310-BASE)[0] == 0x6A6670
cases = []
for resolution in (1, 1080/768, 1440/768):
    for uniform in (1, 1.5):
        for right in (0, 1):
            original = Original()
            original.words(HUD+0x408, CANVAS)
            original.words(HUD+0x98, WORLD)
            original.floats(WORLD+0x2B4, 100)
            original.words(STRING, TEXT, 2, 2)
            original.vm.mem_write(TEXT, '0\0'.encode('utf-16le'))
            original.words(STACK, STOP, STRING)
            height = 60*resolution*uniform
            original.floats(STACK+8, 600, 20, height, -100)
            original.words(STACK+24, right)
            original.vm.reg_write(UC_X86_REG_ECX, HUD)
            original.vm.emu_start(0x6A51A0, STOP, count=10000)
            assert len(original.draws) == 1
            draw = original.draws[0]
            assert abs(draw['scale_x']-height/20) < 1e-5
            assert draw['scale_x'] == draw['scale_y']
            assert draw['y'] == 20
            assert abs(draw['x']-(600-right*100*height/20)) < 1e-4
            assert original.vm.reg_read(UC_X86_REG_ESP) == STACK+28
            cases.append({'resolution': resolution, 'uniform_hud': uniform, 'right': right, **draw})

# Execute the real native weapon-box size arithmetic between two instruction
# boundaries. This proves the native pair, not the runtime property offsets.
weapon_cases = []
for scale in (1, 1.40625, 1.875):
    original = Original()
    original.floats(HUD+0x52C, .8)
    original.floats(HUD+0x538, scale, scale)
    original.floats(HUD+0x5D4, 1.25)
    original.floats(STACK+0x28, 1)
    original.vm.reg_write(UC_X86_REG_ESI, HUD)
    original.vm.emu_start(0x6A6B55, 0x6A6BDB, count=1000)
    width, height = original.number(STACK+0x24), original.number(STACK+0x1C)
    authored_w, authored_h = original.number(0x17A9BF0), original.number(0x175B604)
    assert abs(width/(authored_w*scale)-1) < 1e-6
    assert abs(height/(authored_h*scale)-1) < 1e-6
    weapon_cases.append({'native_equal_input_scale': scale, 'width': width, 'height': height,
                         'authored_width': authored_w, 'authored_height': authored_h})

result = {'original_exe_sha256': expected, 'uthud_constructor': '0x616790', 'vtable': hex(vtable),
          'glow_text_body': '0x6a51a0', 'weapon_bar_body': '0x6a6670',
          'native_glow_text_cases': cases, 'native_weapon_box_cases': weapon_cases,
          'limits': ['Font measurement and final text submission are stubbed; no raster/texture renderer.',
                     'Weapon arithmetic executes original middle block; equal runtime field identity needs metadata/source validation.',
                     'Original UTTeamHUD ScriptText independently establishes its mixed ResolutionScaleX/ResolutionScale draw formula.']}
(OUT / 'original-draw-receipt.json').write_text(json.dumps(result, indent=2)+'\n')
print('PASS 12 original native DrawGlowText complete calls; 3 original weapon-box dimension blocks; pinned original constructor/vtable slots')
