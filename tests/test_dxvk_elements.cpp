/* Actual old/new elements + production memory/Argument/invoke; native setter is
 * a fixture boundary recording exact calls, HRESULT and float-register bits. */
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>
#undef assert
#define assert(value) do { if(!(value)) { fprintf(stderr,"assert line %d: %s\n",__LINE__,#value);fflush(stderr);exit(3); } } while(0)
using UINT=uint32_t;
using HRESULT=int32_t;
#define SUCCEEDED(value) ((value)>=0)
#include "elements_kind.inc"
template<class T>struct KindOf;
struct Span { uint32_t address,bytes;int write;
  bool operator==(const Span &o)const{return address==o.address&&bytes==o.bytes&&write==o.write;} };
static std::array<uint8_t,65536> guest;
static std::vector<Span> spans;
static bool deny_read,read_only;
extern "C" void *ut3_dxvk_guest_memory(uint32_t address,uint32_t bytes,int write) {
  spans.push_back({address,bytes,write});
  if(bytes&&(deny_read||uint64_t(address)+bytes>guest.size()||(write&&read_only)))throw std::runtime_error("fixture guest access");
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(guest.data())+address);
}
struct NativeSetter {
  std::array<uint8_t,4096> registers{};
  unsigned calls=0,method=0;uint32_t start=0,count=0;uintptr_t pointer=0;
  HRESULT configured=0;
  HRESULT call(unsigned m,UINT s,const float *p,UINT n) {
    ++calls;method=m;start=s;count=n;pointer=reinterpret_cast<uintptr_t>(p);
    if(n>256||s>256-n||(!p&&n))return HRESULT(0x8876086Cu);
    if(n)memcpy(registers.data()+size_t(s)*16,p,size_t(n)*16);
    return configured;
  }
  HRESULT vertex(UINT s,const float*p,UINT n){return call(94,s,p,n);}
  HRESULT pixel(UINT s,const float*p,UINT n){return call(109,s,p,n);}
};
namespace baseline {
template<class T>T*object(uint32_t);template<class T>uint32_t wrap(T*);
#include "elements_reference_actual.inc"
}
namespace candidate {
template<class T>T*object(uint32_t);template<class T>uint32_t wrap(T*);
#include "elements_candidate_actual.inc"
}
struct Result {
  std::array<uint8_t,4096> registers;std::vector<Span> spans;
  unsigned calls,method;uint32_t start,count,returned;uintptr_t pointer;std::string exception;
  bool operator==(const Result&o)const {return registers==o.registers&&spans==o.spans&&calls==o.calls&&method==o.method&&
    start==o.start&&count==o.count&&returned==o.returned&&pointer==o.pointer&&exception==o.exception;}
};
static Result run(bool optimized,unsigned slot,uint32_t start,uint32_t address,uint32_t count,HRESULT result,bool deny,bool ro) {
  for(unsigned i=0;i<guest.size();++i)guest[i]=uint8_t(i*31+7);
  const uint32_t bits[]={0x7FC12345u,0x80000000u,0x7F800000u,0xFF800000u,0x00000001u};
  memcpy(guest.data()+4096,bits,sizeof bits);auto before=guest;
  deny_read=deny;read_only=ro;spans.clear();NativeSetter native;native.configured=result;
  uint32_t args[]={start,address,count};uint32_t out=0xDEADBEEFu;std::string error;
  const char*name=slot==94?"SetVertexShaderConstantF":"SetPixelShaderConstantF";
  try {
    if(optimized) {candidate::Context c{Kind::IDirect3DDevice9,&native,slot,args};
      out=slot==94?candidate::invoke(c,&NativeSetter::vertex,name):candidate::invoke(c,&NativeSetter::pixel,name);}
    else {baseline::Context c{Kind::IDirect3DDevice9,&native,slot,args};
      out=slot==94?baseline::invoke(c,&NativeSetter::vertex,name):baseline::invoke(c,&NativeSetter::pixel,name);}
  }catch(const std::exception&e){error=e.what();}
  assert(guest==before);
  for(const auto &span:spans)assert(!span.write);
  return {native.registers,spans,native.calls,native.method,native.start,native.count,out,native.pointer,error};
}
struct Metadata {Kind kind;unsigned slot;const char*name;};
static const Metadata metadata[]={
#include "elements_methods.inc"
};
int main() {
  static_assert(sizeof(size_t)==8&&sizeof(float)==4&&sizeof(UINT)==4);
  size_t cases=0;
  for(unsigned slot:{94u,109u})for(uint32_t start:{0u,1u,255u,256u,UINT32_MAX})
  for(uint32_t address:{0u,4096u,4097u,65520u,65536u,UINT32_MAX})
  for(uint32_t count:{0u,1u,2u,16u,256u,0x0FFFFFFFu,0x10000000u,UINT32_MAX})
  for(uint32_t status:{0u,1u,0x8876086Cu})for(bool deny:{false,true})for(bool ro:{false,true}) {
    Result old=run(false,slot,start,address,count,HRESULT(status),deny,ro);
    Result now=run(true,slot,start,address,count,HRESULT(status),deny,ro);assert(old==now);++cases;
    if(!now.exception.empty())assert(!now.calls);
    else {assert(now.calls==1&&now.method==slot&&now.start==start&&now.count==count);
      assert(now.pointer==(address?reinterpret_cast<uintptr_t>(guest.data())+address:0));}
  }
  size_t fallback=0;
  for(const auto &method:metadata)for(size_t index:{0u,1u,2u,3u})for(uint32_t count:{0u,1u,17u,UINT32_MAX}) {
    uint32_t args[]={13,4096,count};uint32_t value=7;memcpy(guest.data()+4096,&value,4);deny_read=false;read_only=true;
    baseline::Context old{method.kind,nullptr,method.slot,args};candidate::Context now{method.kind,nullptr,method.slot,args};
    assert(baseline::elements(old,method.name,index)==candidate::elements(now,method.name,index));++fallback;
  }
  printf("PASS exact old/new production marshaling: %zu combinations, real-once/HRESULT/type/count/NULL/zero/overflow/read-only/bounds/NaN bits/input preserved; %zu all-method/index/count fallback cases\n",cases,fallback);
}
