"""Generate x86 stack thunks and typed D3D9 dispatch from the pinned native headers."""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/dxvk"
INTERFACES = ["IUnknown", "IDirect3D9", "IDirect3DDevice9", "IDirect3DResource9", "IDirect3DBaseTexture9", "IDirect3DTexture9", "IDirect3DCubeTexture9", "IDirect3DVolumeTexture9", "IDirect3DVolume9", "IDirect3DSurface9", "IDirect3DVertexBuffer9", "IDirect3DIndexBuffer9", "IDirect3DVertexDeclaration9", "IDirect3DVertexShader9", "IDirect3DPixelShader9", "IDirect3DStateBlock9", "IDirect3DQuery9", "IDirect3DSwapChain9"]
SPECIAL = {"QueryInterface", "AddRef", "Release", "CreateDevice", "CreateAdditionalSwapChain", "Reset", "TestCooperativeLevel", "GetPresentParameters", "Present", "GetCreationParameters", "GetContainer", "Lock", "Unlock", "LockRect", "UnlockRect", "LockBox", "UnlockBox", "CreateVertexShader", "CreatePixelShader", "CreateVertexDeclaration", "DrawPrimitiveUP", "DrawIndexedPrimitiveUP", "GetFunction", "GetData", "GetPrivateData", "SetPrivateData", "FreePrivateData", "GetAdapterIdentifier", "GetAdapterMonitor", "GetDC", "ReleaseDC", "RegisterSoftwareDevice"}

def main():
    source = (ROOT / "reference/dxvk/include/native/directx/d3d9.h").read_text(encoding="utf-8")
    methods = {}
    for interface in INTERFACES:
        if interface == "IUnknown":
            items = [("HRESULT", "QueryInterface", "REFIID iid, void** out"), ("ULONG", "AddRef", ""), ("ULONG", "Release", "")]
        else:
            found = re.search(r"DECLARE_INTERFACE_\(" + interface + r",\w+\)\s*\{(.*?)\n\};", source, re.S)
            if not found:
                raise ValueError("D3D9 interface not found: " + interface)
            items = []
            for match in re.finditer(r"STDMETHOD(?:_\(([^,]+),\s*(\w+)\)|\((\w+)\))\s*\(THIS(_)?\s*(.*?)\)\s*PURE;", found.group(1), re.S):
                result, name, hr_name, has_args, arguments = match.groups()
                items.append(((result or "HRESULT").strip(), name or hr_name, arguments.strip() if has_args else ""))
        if [item[1] for item in items[:3]] != ["QueryInterface", "AddRef", "Release"]:
            raise ValueError("Invalid IUnknown prefix: " + interface)
        methods[interface] = items
    c = ["/* Generated from pinned DXVK Native d3d9.h. */"]
    cpp = ["// Generated typed native D3D9 slots.", "enum class Kind : unsigned {" + ",".join(INTERFACES) + ",Count};", "template<class T> struct KindOf;", "static Kind kind_for_iid(REFIID iid) {"]
    cpp += [f"if(iid==__uuidof({name})) return Kind::{name};" for name in INTERFACES]
    cpp += ["return Kind::Count;}"]
    for kind, interface in enumerate(INTERFACES):
        cpp += [f"template<> struct KindOf<{interface}> {{static constexpr Kind value=Kind::{interface};}};"]
        for slot, (result, name, arguments) in enumerate(methods[interface]):
            count = 0 if not arguments else len(arguments.split(","))
            assert count <= 14, (interface, name, count)
            finish = "dxvk_return_float(result);" if result == "float" else "g_eax=result;"
            c += [f"static void dxvk_{kind}_{slot}(void) {{uint32_t a[{max(count,1)}]={{" + (",".join(f"ARG({i+1})" for i in range(count)) if count else "0") + f"}};uint32_t result=dxvk_guest_call(THIS_,{kind},{slot},a);{finish}g_esp+={8+4*count}u;}}"]
        c += [f"static const void_fn dxvk_methods_{kind}[]={{" + ",".join(f"dxvk_{kind}_{slot}" for slot in range(len(methods[interface]))) + "};"]
    c += ["static void_com_class dxvk_classes[]={"]
    c += [f'{{"DXVK::{interface}",{len(methods[interface])},dxvk_methods_{kind},0}},' for kind,interface in enumerate(INTERFACES)]
    c += ["};"]
    dispatch = []
    special = []
    for interface in INTERFACES:
        dispatch.append(f"case Kind::{interface}: switch(slot) {{")
        special.append(f"case Kind::{interface}: switch(slot) {{")
        for slot, (result, name, arguments) in enumerate(methods[interface]):
            if name in SPECIAL:
                if name in {"SetPrivateData", "GetPrivateData", "FreePrivateData"}:
                    dispatch.append(f'case {slot}: return private_data(c,static_cast<{interface}*>(c.self),"{name}");')
                else:
                    special.append(f'case {slot}: name="{name}";break;')
                continue
            # Windows shared-resource HANDLEs and GDI cursor/DC services are not Android capabilities.
            if name in {"CreateTexture", "CreateVolumeTexture", "CreateCubeTexture", "CreateVertexBuffer", "CreateIndexBuffer", "CreateRenderTarget", "CreateDepthStencilSurface", "CreateOffscreenPlainSurface"}:
                call = f'create_resource(c,&{interface}::{name},"{name}")'
            else:
                call = f'invoke(c,&{interface}::{name},"{name}")'
            dispatch.append(f"case {slot}: return {call};")
        dispatch.append("default: break;}break;")
        special.append("default: break;}break;")
    OUT.mkdir(parents=True, exist_ok=True)
    for name, value in [("guest_methods.inc", "\n".join(c)), ("bridge_metadata.inc", "\n".join(cpp)), ("bridge_dispatch.inc", "\n".join(dispatch)), ("bridge_special_names.inc", "\n".join(special))]:
        path=OUT/name; data=(value+"\n").encode()
        if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)
    (OUT/"bridge_slots.json").write_text(json.dumps({interface:[{'name':name,'result':result,'arguments':arguments}for result,name,arguments in items]for interface,items in methods.items()},indent=2)+"\n")
    print("Generated",sum(len(items)for items in methods.values()),"typed D3D9 slots across",len(methods),"interfaces")

if __name__ == "__main__":main()
