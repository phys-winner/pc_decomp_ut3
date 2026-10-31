"""HOST old/new production marshaling oracle plus strict Android compilation."""
import os
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/dxvk-elements-probe'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()

class ElementsTest(unittest.TestCase):
    def test_production_boundary_and_metadata(self):
        OUT.mkdir(parents=True,exist_ok=True)
        source=(ROOT/'android/dxvk/bridge.cpp').read_text()
        before=(OUT/'before/bridge.cpp').read_text()
        reference=(ROOT/'tests/dxvk_elements_reference.inc').read_text()
        def elements(text):return text[text.index('size_t elements('):text.index('template<class T,class=void>struct IsInterface:')]
        self.assertEqual(elements(before).strip(),reference[reference.index('size_t elements('):].strip())
        guard='  if(index==1&&c.kind==Kind::IDirect3DDevice9&&(c.slot==94||c.slot==109))return size_t(c.a[2])*4;\n'
        self.assertEqual(source.count(guard),1)
        self.assertEqual(source.replace(guard,''),before)
        context='struct Context{Kind kind;void *self;unsigned slot;const uint32_t *a;};\n'
        self.assertIn(context.strip(),source)
        memory=source[source.index('template<class T>T *memory('):source.index('uint32_t narrow_size(')]
        arguments=source[source.index('template<class T,class=void>struct IsInterface:'):source.index('D3DPRESENT_PARAMETERS native_parameters(')]
        for name,code in [('reference',elements(before)),('candidate',elements(source))]:
            (OUT/f'elements_{name}_actual.inc').write_text(context+memory+code+arguments)
        metadata=(ROOT/'build/dxvk/bridge_metadata.inc').read_text()
        kind=re.search(r'enum class Kind : unsigned \{[^\n]+',metadata).group(0)
        (OUT/'elements_kind.inc').write_text(kind+'\n')
        names=kind[kind.index('{')+1:kind.index('}')].split(',')
        self.assertEqual(names[2],'IDirect3DDevice9')
        dispatch=(ROOT/'build/dxvk/bridge_dispatch.inc').read_text();current=None;rows=[];seen={}
        for line in dispatch.splitlines():
            match=re.match(r'case Kind::(\w+):',line)
            if match:current=match.group(1)
            match=re.match(r'case (\d+): return (?:invoke|create_resource)\(c,&\w+::(\w+),"([^"]+)"\);',line)
            if match:
                slot,method,text=match.groups();self.assertEqual(method,text)
                rows.append(f'{{Kind::{current},{slot},"{method}"}},');seen[(current,int(slot))]=method
        self.assertEqual(seen[('IDirect3DDevice9',94)],'SetVertexShaderConstantF')
        self.assertEqual(seen[('IDirect3DDevice9',109)],'SetPixelShaderConstantF')
        header=(ROOT/'reference/dxvk/include/native/directx/d3d9.h').read_text()
        for method in ['SetVertexShaderConstantF','SetPixelShaderConstantF']:
            self.assertIn(f'STDMETHOD({method})(THIS_ UINT reg_idx, const float *data, UINT count) PURE;',header)
        (OUT/'elements_methods.inc').write_text('\n'.join(rows)+'\n')
        spec=importlib.util.spec_from_file_location('elements_host',ROOT/'tests/host_environment.py')
        host=importlib.util.module_from_spec(spec);spec.loader.exec_module(host);env,linker=host.environment('x64')
        # Installed VS2026 STL requires Clang20; use its matching installed MSVC
        # for HOST execution, then the exact production NDK for ARM64 below.
        # The C memory callback throws only in this oracle to represent a
        # production trap; /EHs keeps those synthetic exceptions observable.
        commands=[[str(linker.parent/'cl.exe'),'/nologo','/O2','/std:c++17','/EHs','/W4','/D_CRT_SECURE_NO_WARNINGS',
                   '/I'+str(OUT),'/c',str(ROOT/'tests/test_dxvk_elements.cpp'),'/Fo'+str(OUT/'test.obj')],
                  [str(linker),'/nologo',str(OUT/'test.obj'),'/OUT:'+str(OUT/'test.exe')],[str(OUT/'test.exe')]]
        outputs=[]
        for command in commands:
            result=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,timeout=60)
            outputs.append((result.stdout+result.stderr).decode(errors='replace'));self.assertEqual(result.returncode,0,outputs[-1])
        (OUT/'host.log').write_text('\n'.join(outputs));print(outputs[-1].strip())
        llvm=Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk')) / 'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin'
        command=[str(llvm/'clang++.exe'),'--target=aarch64-linux-android29','-std=c++17','-O2','-fPIC','-Wall','-Wextra','-Werror',
                 '-Wno-extern-c-compat','-Wno-missing-field-initializers']
        for path in ['reference/dxvk/include/native/windows','reference/dxvk/include/native/directx','reference/dxvk/include/vulkan/include','build/dxvk']:
            command+=['-I',str(ROOT/path)]
        command+=['-c',str(ROOT/'android/dxvk/bridge.cpp'),'-o',str(OUT/'bridge-strict.o')]
        result=subprocess.run(command,cwd=ROOT,capture_output=True,timeout=60)
        self.assertEqual(result.returncode,0,(result.stdout+result.stderr).decode(errors='replace'));commands.append(command)
        pins=['android/dxvk/bridge.cpp','tests/test_dxvk_elements.cpp','tests/test_dxvk_elements.py','tests/dxvk_elements_reference.inc',
              'build/dxvk/bridge_metadata.inc','build/dxvk/bridge_dispatch.inc','reference/dxvk/include/native/directx/d3d9.h']
        report={'status':'PASS_HOST_EXACT_OLD_NEW_MARSHALING_AND_STRICT_ANDROID_ARM64_COMPILE','scope':'Only2constantF numeric size classifiers; actual production memory/Argument/invoke; native setter is fixture boundary, no real GPU/device/FPS claim.',
                'source_sha256':{p:sha(ROOT/p) for p in pins},'commands':commands,'host_log_sha256':sha(OUT/'host.log'),
                'android_object_sha256':sha(OUT/'bridge-strict.o'),'metadata_methods':len(rows),'device_executed':False}
        (OUT/'receipt.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':unittest.main()
