"""Compile AMD's scalar FSR1 kernels for the port's shared D3D9 shader path."""
import ctypes
from ctypes import wintypes as W
from pathlib import Path
import re
import struct

ROOT=Path(__file__).resolve().parents[1]

def compile_shader(source, entry, profile='ps_3_0'):
    dll=ctypes.WinDLL('d3dcompiler_47.dll')
    function=dll.D3DCompile
    function.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_char_p,ctypes.c_void_p,ctypes.c_void_p,
        ctypes.c_char_p,ctypes.c_char_p,W.UINT,W.UINT,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_void_p)]
    function.restype=ctypes.c_long
    code,error=ctypes.c_void_p(),ctypes.c_void_p()
    data=source.encode('utf-8')
    result=function(data,len(data),b'ut3_fsr1',None,None,entry.encode(),profile.encode(),1<<15,0,ctypes.byref(code),ctypes.byref(error))
    def blob(p):
        table=ctypes.cast(p,ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        address=ctypes.WINFUNCTYPE(ctypes.c_void_p,ctypes.c_void_p)(table[3])(p)
        size=ctypes.WINFUNCTYPE(ctypes.c_size_t,ctypes.c_void_p)(table[4])(p)
        contents=ctypes.string_at(address,size)
        ctypes.WINFUNCTYPE(W.ULONG,ctypes.c_void_p)(table[2])(p)
        return contents
    errors=blob(error) if error else b''
    if result:raise RuntimeError(errors.decode('utf-8',errors='replace'))
    if errors:print(errors.decode('utf-8',errors='replace'))
    return blob(code)

def main():
    source=(ROOT/'reference/fsr1/ffx_fsr1.h').read_text(encoding='utf-8')
    easu=source.split('#if defined(A_GPU)&&defined(FSR_EASU_F)')[1].split('#endif')[0]
    rcas=source.split('#if defined(A_GPU)&&defined(FSR_RCAS_F)')[1].split('////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////')[0].rsplit('#endif',1)[0]
    # SM3 has float constants and individual samples, no integer bit casts or texture gather.
    preamble='''
#define AF1 float
#define AF2 float2
#define AF3 float3
#define AF4 float4
#define AU2 float2
#define AU4 float4
#define ASU2 float2
#define AP1 bool
#define AF1_(x) (x)
#define AF2_(x) float2(x,x)
#define AF3_(x) float3(x,x,x)
#define AF4_(x) float4(x,x,x,x)
#define AF1_AU1(x) (x)
#define AF2_AU2(x) (x)
#define ARcpF1(x) (((x)<0?-1.0:1.0)/max(abs(x),1.e-8))
#define APrxLoRcpF1(x) (1.0/max(x,1.e-8))
#define APrxMedRcpF1(x) ARcpF1(x)
#define APrxLoRsqF1(x) rsqrt(max(x,1.e-8))
#define ASatF1(x) saturate(x)
#define AMin3F1(a,b,c) min(a,min(b,c))
#define AMin3F3(a,b,c) min(a,min(b,c))
#define AMax3F1(a,b,c) max(a,max(b,c))
#define AMax3F3(a,b,c) max(a,max(b,c))
#define FSR_RCAS_LIMIT (0.25-1.0/16.0)
sampler2D image:register(s0);
float4 con0:register(c0),con1:register(c1),con2:register(c2),con3:register(c3),size:register(c4);
float4 gatherChannel(float2 p,int channel){
    float2 q=(floor(p*size.xy-0.5)+0.5)/size.xy;
    return float4(tex2Dlod(image,float4(q+float2(0,1)*size.zw,0,0))[channel],
                  tex2Dlod(image,float4(q+float2(1,1)*size.zw,0,0))[channel],
                  tex2Dlod(image,float4(q+float2(1,0)*size.zw,0,0))[channel],
                  tex2Dlod(image,float4(q,0,0))[channel]);
}
float4 FsrEasuRF(float2 p){return gatherChannel(p,0);}
float4 FsrEasuGF(float2 p){return gatherChannel(p,1);}
float4 FsrEasuBF(float2 p){return gatherChannel(p,2);}
float4 FsrRcasLoadF(float2 p){return tex2Dlod(image,float4((p+0.5)*size.zw,0,0));}
void FsrRcasInputF(inout float r,inout float g,inout float b){}
'''
    programs={
        'fsr_easu':preamble+easu+'\nfloat4 main(float2 p:VPOS):COLOR0{float3 c;FsrEasuF(c,floor(p),con0,con1,con2,con3);return float4(c,1);}',
        'fsr_rcas':preamble+rcas+'\nfloat4 main(float2 p:VPOS):COLOR0{float3 c;FsrRcasF(c.r,c.g,c.b,floor(p),con0);return float4(c,1);}',
        'postfx_aa':'''sampler2D image:register(s0);float4 size:register(c4);
float4 main(float2 p:VPOS):COLOR0{
    float2 uv=(floor(p)+0.5)*size.zw;
    float3 rgb=tex2Dlod(image,float4(uv,0,0)).rgb;
    float3 nw=tex2Dlod(image,float4(uv+float2(-1,-1)*size.zw,0,0)).rgb;
    float3 ne=tex2Dlod(image,float4(uv+float2(1,-1)*size.zw,0,0)).rgb;
    float3 sw=tex2Dlod(image,float4(uv+float2(-1,1)*size.zw,0,0)).rgb;
    float3 se=tex2Dlod(image,float4(uv+float2(1,1)*size.zw,0,0)).rgb;
    float3 luma=float3(.299,.587,.114);
    float m=dot(rgb,luma),a=dot(nw,luma),b=dot(ne,luma),c=dot(sw,luma),d=dot(se,luma);
    float lo=min(m,min(min(a,b),min(c,d))),hi=max(m,max(max(a,b),max(c,d)));
    float2 dir=float2(-((a+b)-(c+d)),(a+c)-(b+d));
    float reduce=max((a+b+c+d)*(.25*.125),1./128.);
    dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),-8,8)*size.zw;
    float3 first=.5*(tex2Dlod(image,float4(uv+dir*(1./3.-.5),0,0)).rgb+tex2Dlod(image,float4(uv+dir*(2./3.-.5),0,0)).rgb);
    float3 second=first*.5+.25*(tex2Dlod(image,float4(uv-dir*.5,0,0)).rgb+tex2Dlod(image,float4(uv+dir*.5,0,0)).rgb);
    float lum=dot(second,luma);return float4((lum<lo||lum>hi)?first:second,1);
}''',
    }
    out=ROOT/'build/postfx';out.mkdir(parents=True,exist_ok=True)
    generated='/* AMD FidelityFX FSR1 scalar kernels, MIT. Generated by tools/build_postfx_shaders.py. */\n'
    for name,text in programs.items():
        (out/(name+'.hlsl')).write_text(text,encoding='utf-8')
        code=compile_shader(text,'main')
        (out/(name+'.bin')).write_bytes(code)
        words=struct.unpack('<'+'I'*(len(code)//4),code)
        generated+='static const uint32_t '+name+'[]={\n'+',\n'.join('  '+','.join(f'0x{w:08x}u' for w in words[i:i+8]) for i in range(0,len(words),8))+'\n};\n'
        print(name,len(code),'bytes')
    (ROOT/'android/host/postfx_shaders.inc').write_text(generated,encoding='utf-8')

if __name__=='__main__':main()
