"""Scratch-only native Canvas draw proof for a scoped crosshair proposal."""
import json
import hashlib
import importlib.util
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
shared = ROOT / 'tests/hud_draw_original.py'
namespace = {'__file__': str(shared)}
exec(compile(shared.read_text().split('vtable = 0x17CD310')[0], str(shared), 'exec'), namespace)
Original = namespace['Original']
HEAP, STACK, STOP, CANVAS = [namespace[x] for x in ('HEAP', 'STACK', 'STOP', 'CANVAS')]
EAX, ECX, ESP = [namespace['UC_X86_REG_'+x] for x in ('EAX', 'ECX', 'ESP')]
TEXTURE, NATIVE, VTABLE, FCANVAS, FRAME, CODE = [HEAP+x for x in (0x8000,0x9000,0xA000,0xB000,0xC000,0xD000)]
GET_WIDTH, GET_HEIGHT = STOP+0x100, STOP+0x200


class CanvasOriginal(Original):
    def __init__(self):
        super().__init__()
        self.tiles = []
        self.words(TEXTURE+0xB8, NATIVE)
        self.words(NATIVE, VTABLE)
        self.words(VTABLE+0x20, GET_WIDTH, GET_HEIGHT)
        self.words(CANVAS+0x6C, FCANVAS)
        # Execute the pinned real FloatConst/ObjectConst registrations, rather
        # than mirror their handler addresses in this fixture.
        for entry in (0x1702E20, 0x1702F10):
            self.words(STACK, STOP)
            self.vm.reg_write(ESP, STACK)
            self.vm.emu_start(entry, STOP, count=100000)
        assert self.word(0x1DE67B8) == 0xBC1D20
        assert self.word(0x1DE67C0) == 0xBC1D40

    def step(self, vm, pc, size, _):
        sp = vm.reg_read(ESP)
        if pc in (GET_WIDTH, GET_HEIGHT):
            vm.reg_write(EAX, 1024)
            self.finish(0)
        elif pc == 0xBF2AA0:  # Colour gamma conversion, unrelated to geometry.
            out = vm.reg_read(ECX)
            self.floats(out, 1, 1, 1, 1)
            vm.reg_write(EAX, out)
            self.finish(4)
        elif pc == 0xEF55E0:  # Final FCanvas batch boundary only.
            assert self.word(sp+4) == FCANVAS
            assert self.word(sp+44) == NATIVE
            self.tiles.append([self.number(sp+8+4*i) for i in range(8)])
            self.finish(0)

    def invoke(self, address, payload):
        self.vm.mem_write(CODE, payload+b'\x16\x00')
        self.words(FRAME+8, CANVAS, CODE)
        self.words(STACK, STOP, FRAME, 0)
        self.vm.reg_write(ESP, STACK)
        self.vm.reg_write(ECX, CANVAS)
        self.vm.emu_start(address, STOP, count=10000)
        assert self.vm.reg_read(ESP) == STACK+12
        assert self.word(FRAME+12) == CODE+len(payload)+1

    def setpos(self, x, y):
        self.invoke(0xEEF280, b''.join(b'\x1E'+struct.pack('<f',v) for v in (x,y)))

    def tile(self, w, h, ul, vl):
        self.invoke(0xEF6EC0, b'\x20'+struct.pack('<I',TEXTURE)+
                    b''.join(b'\x1E'+struct.pack('<f',v) for v in (w,h,17,23,ul,vl)))


def draw(width, height, size_x, size_y, ul, vl, proposed=False, org=0, physical_draw=False):
    original = CanvasOriginal()
    clip = 1024 if proposed else width
    shift = (width-1024)*.5 if proposed else 0
    original.floats(CANVAS+0x40, org+shift, 31, clip, height)
    x, y = clip*.5-size_x*.5, height*.5-size_y*.5
    for shadow in (1, 0):
        original.setpos(x+shadow, y+shadow)
        if physical_draw:
            original.floats(CANVAS+0x40,org,31,width,height)
            original.floats(CANVAS+0x50,original.number(CANVAS+0x50)+shift)
        original.tile(size_x, size_y, ul, vl)
        if physical_draw:
            original.floats(CANVAS+0x40,org+shift,31,clip,height)
            original.floats(CANVAS+0x50,original.number(CANVAS+0x50)-shift)
    return original.tiles, original.number(CANVAS+0x50)+shift


weapon = (ROOT/'research/native-settings-source/UTWeapon.uc').read_text()
assert 'CrosshairSize.X = CrosshairSize.Y * ( CrossHairCoordinates.UL / CrossHairCoordinates.VL ) * H.Canvas.ClipX/1024;' in weapon
assert 'X = H.Canvas.ClipX * 0.5;' in weapon
cases = []
for width, height in ((800,600),(1440,1080),(1920,1080),(2400,1080)):
    for configured in (.5,1,1.5):
        for pickup in (1,1.75):
            for ul, vl in ((16,16),(64,32),(32,64)):
                sy = configured*vl*pickup*height/768
                sx = sy*ul/vl
                assert sx+2 <= min(width,1024)
                for org in (0,120):
                    expected, expected_cur = draw(width,height,sx,sy,ul,vl,org=org)
                    candidate, candidate_cur = draw(width,height,sx,sy,ul,vl,True,org)
                    assert len(candidate) == len(expected) == 2
                    for a,b in zip(candidate,expected):
                        assert all(abs(x-y)<.0003 for x,y in zip(a,b)), (width,height,a,b)
                    assert abs(expected_cur-candidate_cur)<.0003
                    # Full texture UVs survive native clamp; both shadow and
                    # main tile retain authoredUL/VL shape and physical center.
                    assert abs(candidate[1][2]/candidate[1][3]-ul/vl)<1e-5
                    assert abs(candidate[1][0]+candidate[1][2]*.5-(org+width*.5))<.0003
                    assert abs(candidate[1][1]+candidate[1][3]*.5-(31+height*.5))<.0003
                    cases.append({'viewport':[width,height],'configured':configured,'pickup':pickup,
                                  'uv_size':[ul,vl],'origin_x':org,'size':[sx,sy]})

# A virtualClip alone is NOT valid for every possible oversized/modded reticle:
# execute a real native clipping counterexample, to require a bounded guard or
# a later exact nativeargument correction instead of hiding this limitation.
expected,_ = draw(2400,1080,1200,100,1200,100)
candidate,_ = draw(2400,1080,1200,100,1200,100,True)
assert candidate[1][2] != expected[1][2]
oversized = {'intended':expected,'virtual_clip':candidate}
physical_cases=[]
for width,height in ((800,600),(1440,1080),(1920,1080),(2400,1080)):
    for sx,sy in ((20,20),(1200,100),(4000,200),(100,4000)):
        for org in (0,120):
            expected,expected_cur=draw(width,height,sx,sy,sx,sy,org=org)
            physical,physical_cur=draw(width,height,sx,sy,sx,sy,True,org,True)
            for a,b in zip(physical,expected):
                assert all(abs(x-y)<.0003 for x,y in zip(a,b)),(width,height,a,b)
            assert abs(physical_cur-expected_cur)<.0003
            physical_cases.append({'viewport':[width,height],'glyph':[sx,sy],'origin_x':org})
result = {'status':'PASS_ORIGINAL_NATIVE_DRAW_CENTER_STYLE_AND_PHYSICAL_CLIPPING',
          'original_exe_sha256':namespace['expected'],'cases':len(cases),
          'real_entries':['0x1702e20','0x1702f10','0xeef280','0xef6ec0','0xef6870'],
          'details':cases,'oversized_counterexample':oversized,
          'physical_native_draw_cases':physical_cases,
          'limits':['OriginalUTWeapon geometry is source-checked and evaluated by fixture; originalweapon bytecode itself is not executed.',
                    'Real Canvas Float/ObjectConst VM decoding,SetPos,DrawTile and nativeclipping execute to finalFCanvas batch boundary.',
                    'Texture dimensions,colour conversion and batch submission are stub boundaries.',
                    'Clip-onlycandidate requires prospectivewidth+shadowmargin<=min(physicalClipX,1024); otherwise clipping differs.',
                    'RestoringphysicalClipX/OrgX and convertingCurX justaroundrealexecDrawTile preservesoriginalclipping, includingoversizedcases.',
                    'The nativegeometry oracle doesnotexecute Cidentity/unwindguards; separateproductionfixture covers those paths.']}

# Independent original registration/decoder proof for the production metadata
# boundaries. No copied production constants determine the observed results.
class RegistrationOriginal(CanvasOriginal):
    def __init__(self):
        super().__init__();self.classes=[]
        self.vm.mem_map(0,4096);self.words(0,0x12345678)
        self.words(0x1DAC458,HEAP+0xE000);self.words(HEAP+0xE000,HEAP+0xE100)
        self.words(HEAP+0xE104,STOP+0x300)

    def step(self,vm,pc,size,extra):
        sp=vm.reg_read(ESP)
        if pc==STOP+0x300:
            assert self.word(sp+4)==0x17C and self.word(sp+8)==8
            vm.reg_write(EAX,HEAP+0xF000);self.finish(8)
        elif pc==0xB7A1F0:
            address=self.word(sp+20);characters=[]
            for index in range(32):
                value=struct.unpack('<H',vm.mem_read(address+2*index,2))[0]
                if not value:break
                characters.append(chr(value))
            self.classes.append((''.join(characters),self.word(sp+40)))
            vm.reg_write(EAX,HEAP+0xF000);self.finish(48)
        else:super().step(vm,pc,size,extra)

registration=RegistrationOriginal();registration.words(0x1D4B260,0)
registration.words(STACK,STOP);registration.vm.reg_write(ESP,STACK)
registration.vm.emu_start(0x5EAE80,0x5EAE9B,count=10000)
assert registration.classes==[('UTWeapon',0x6BAE50)]
assert registration.word(0x1D4B260)==HEAP+0xF000 and registration.word(0)==0x12345678
result['weapon_class_registration']={'start':'0x5eae80','store_before':'0x5eae9b','global':'0x1d4b260',
                                     'class':registration.classes[0][0],'constructor':hex(registration.classes[0][1])}

decoder=CanvasOriginal()
for initializer in (0x1701DE0,0x1701E30,0x1702380):
    decoder.words(STACK,STOP);decoder.vm.reg_write(ESP,STACK)
    decoder.vm.emu_start(initializer,STOP,count=100000)
property_address,pc_object,locals_address,output=[HEAP+x for x in (0xE000,0xE500,0xEA00,0xEB00)]
decoder.words(property_address+0x64,0x300)
decoder.words(property_address+0x84,0x20);decoder.words(property_address+0x70,0x40000000)
bool_handler=decoder.word(0x1DE67F4);assert bool_handler==0xBC15A0
for value,expected_value in ((0,0),(0x20,1),(0x40000000,0),(0x40000020,1)):
    decoder.words(pc_object+0x300,value);decoder.words(FRAME+8,pc_object,CODE)
    decoder.vm.mem_write(CODE,b'\x01'+struct.pack('<I',property_address))
    decoder.words(STACK,STOP,FRAME,output);decoder.vm.reg_write(ESP,STACK);decoder.vm.reg_write(ECX,pc_object)
    decoder.vm.emu_start(bool_handler,STOP,count=10000)
    assert decoder.word(output)==expected_value and decoder.word(pc_object+0x300)==value
    assert decoder.word(0x1DAC474)==pc_object+0x300
for offset in (0,4,20):
    decoder.words(property_address+0x64,offset);decoder.words(FRAME+0xC,CODE,locals_address)
    decoder.words(CODE,property_address);decoder.words(STACK,STOP,FRAME,0)
    decoder.vm.reg_write(ESP,STACK)
    decoder.vm.emu_start(decoder.word(0x1DE6740),STOP,count=10000)
    assert decoder.word(0x1DAC474)==locals_address+offset
result['actual_metadata_decoder_cases']={'bool_handler':hex(bool_handler),'bool_mask_offset':'0x84',
                                          'bool_decoy_offset':'0x70','bool_cases':4,'local_cases':3,
                                          'property_offset':'0x64','frame_locals_offset':'0x10'}

spec=importlib.util.spec_from_file_location('crosshair_original_package',ROOT/'tools/research_hidden_content.py')
hc=importlib.util.module_from_spec(spec);spec.loader.exec_module(hc)
cfg=json.loads((ROOT/'config/inputs.json').read_text());source=Path(cfg['source_root'])
evidence=json.loads((ROOT/'research/hidden-content-evidence.json').read_text())
package_path=source/'UTGame/CookedPC/UTGame.u'
package_hash=next(x['sha256'] for x in evidence['scripts'] if x['path']=='UTGame/CookedPC/UTGame.u')
assert hc.sha(package_path)==package_hash
lzo=hc.OriginalLzo(source,namespace['expected']);package=hc.package(package_path,lzo)
argument_index,argument_object=next((i,obj) for i,obj in enumerate(package['objects'],1) if obj['path']=='UTWeapon.ActiveRenderOverlays.H')
caller=next(obj for obj in package['objects'] if obj['path']=='UTWeapon.ActiveRenderOverlays')
payload=hc.read_package_range(package_path.read_bytes(),lzo,caller['serial_offset'],caller['serial_size'])
name_index=package['names'].index('DrawWeaponCrosshair')
operand=b'\x1B'+struct.pack('<II',name_index,0)+b'\x00'+struct.pack('<I',argument_index)+b'\x16'
assert payload.count(operand)==1 and argument_object['class_path']=='Core.ObjectProperty'
result['actual_weapon_caller_operand']={'package_sha256':package_hash,'caller':caller['path'],
    'caller_serial_offset':caller['serial_offset'],'caller_serial_size':caller['serial_size'],
    'operand_offset':payload.index(operand),'operand_hex':operand.hex(),'argument':argument_object['path'],
    'argument_index':argument_index,'argument_kind':argument_object['class_path']}
result['oracle_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
out=ROOT/'build/crosshair-aspect-probe/receipt.json'
out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps(result,indent=2)+'\n')
print('PASS',len(cases),'real Canvas VM/native DrawTile center/UV/style/clip cases;',len(physical_cases),'physicalnativeentrycasesincloversizedclipping; clip-onlyoversizedcounterexampleproven')
print('PASS original UTWeapon class registration; BoolProperty mask84 with70decoy; LocalVariable offset64/FrameLocals10; exact original caller localH operand')
