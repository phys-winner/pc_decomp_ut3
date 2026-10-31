/* Reuse guarded guest memory and native-window stubs from the production bridge fixture. */
#define main bridge_fixture_main
#include "test_dxvk_bridge.cpp"
#undef main

class QueryFixture final:public IDirect3DQuery9 {
public:
  HRESULT result=S_FALSE;DWORD samples=0,bytes=0,flags=0;unsigned calls=0;
  HRESULT QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
  ULONG AddRef()override{return 2;}
  ULONG Release()override{return 1;}
  HRESULT GetDevice(IDirect3DDevice9**)override{return E_NOTIMPL;}
  D3DQUERYTYPE GetType()override{return D3DQUERYTYPE_OCCLUSION;}
  DWORD GetDataSize()override{return 4;}
  HRESULT Issue(DWORD)override{return S_OK;}
  HRESULT GetData(void *out,DWORD size,DWORD options)override {
    ++calls;bytes=size;flags=options;
    if(result==S_OK&&out&&size==4)memcpy(out,&samples,4);
    return result;
  }
};

int main() {
  QueryFixture query;uint32_t args[]={0x3000,4,D3DGETDATA_FLUSH};
  ref<DWORD>(0x3000,true)=0xa5a55a5a;
  assert(ut3_dxvk_native_call(unsigned(Kind::IDirect3DQuery9),&query,7,args)==S_FALSE);
  assert(ref<DWORD>(0x3000)==0xa5a55a5a&&query.calls==1&&query.bytes==4&&query.flags==D3DGETDATA_FLUSH);
  query.result=S_OK;
  for(DWORD samples:{0u,32u,0x80000000u,0xffffffffu}) {
    query.samples=samples;
    assert(ut3_dxvk_native_call(unsigned(Kind::IDirect3DQuery9),&query,7,args)==S_OK);
    assert(ref<DWORD>(0x3000)==samples);
  }
  query.result=D3DERR_INVALIDCALL;
  assert(ut3_dxvk_native_call(unsigned(Kind::IDirect3DQuery9),&query,7,args)==uint32_t(D3DERR_INVALIDCALL));
  assert(ref<DWORD>(0x3000)==0xffffffffu);
  query.result=S_FALSE;args[2]=0;
  assert(ut3_dxvk_native_call(unsigned(Kind::IDirect3DQuery9),&query,7,args)==S_FALSE&&query.flags==0);
  unsigned before=query.calls;args[0]=uint32_t(fixture_memory.size()-1);args[1]=4;
  assert(ut3_dxvk_native_call(unsigned(Kind::IDirect3DQuery9),&query,7,args)==uint32_t(D3DERR_INVALIDCALL));
  assert(query.calls==before);
  puts("PASS production query forwarding: S_FALSE preserves bytes, exact flags/size, genuine zero/positive/unsigned samples, HRESULT errors, rejected guest bounds; no GPU or game");
}
