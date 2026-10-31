/* Same GPU/readback oracle, with every result crossing the actual production
 * native bridge's guarded guest span and Query9/GetData fast dispatch. */
#define main unused_bridge_fixture_main
#include "test_dxvk_bridge.cpp"
#undef main

static HRESULT bridge_query_getdata(IDirect3DQuery9 *query, void *out, DWORD bytes, DWORD flags)
{
    assert(bytes == 4 && out);
    constexpr uint32_t guest = 0x3000;
    uint32_t args[] = {guest, bytes, flags};
    memcpy(memory<uint8_t>(guest, bytes, true), out, bytes);
    uint32_t before = *memory<uint32_t>(guest);
    uint32_t hr = ut3_dxvk_native_call(unsigned(Kind::IDirect3DQuery9), query, 7, args);
    if (hr == S_FALSE) assert(*memory<uint32_t>(guest) == before);
    memcpy(out, memory<uint8_t>(guest, bytes), bytes);
    return HRESULT(hr);
}

#define UT3_ORACLE_QUERY_GETDATA bridge_query_getdata
#define main native_gpu_oracle_main
#include "test_dxvk_occlusion_gpu.cpp"
#undef main

int main()
{
    int result = native_gpu_oracle_main();
    printf("%s production native bridge GetData real GPU oracle; guarded guest output and pending bytes checked\n", result ? "FAIL" : "PASS");
    return result;
}
