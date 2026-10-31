/* The real production exports, module lookup, shader failures and guest ABI. */
#include "host.h"
#include <assert.h>
#include <sys/stat.h>
static unsigned debug_messages;
static void d3dx_test_log(const char *format, ...) { (void)format; ++debug_messages; }
#define void_log d3dx_test_log
#include "../android/host/d3dx9.c"
#undef void_log
#include "../android/host/pe.c"

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static uint32_t target(uint32_t module, const char *name)
{
    uint32_t string = void_gstrdup(name);
    uint32_t result = void_module_proc(module, string);
    void_gfree(string);
    return result;
}

static uint32_t call(uint32_t procedure, unsigned count, const uint32_t *args)
{
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(procedure, count, args, 1);
    assert(g_esp == before);
    return result;
}

static void compile_failure(uint32_t compile)
{
    static const char source[] = "float4 main() : COLOR { return float4(1, 0, 0, 1); }\n";
    uint32_t text = void_gstrdup(source), entry = void_gstrdup("main"), profile = void_gstrdup("ps_3_0");
    uint32_t outputs = void_galloc(20);
    memset(GPTR(outputs), 0xA5, 20);
    uint32_t args[10] = {text, sizeof source - 1, 0, 0, entry, profile, 0, outputs + 4, outputs + 8, outputs + 12};
    assert(call(compile, 10, args) == E_FAIL_);
    assert(MEM32(outputs) == 0xA5A5A5A5 && MEM32(outputs + 16) == 0xA5A5A5A5);
    assert(!MEM32(outputs + 4) && MEM32(outputs + 8) && !MEM32(outputs + 12));
    dxbuffer_t *error = void_com_host(MEM32(outputs + 8));
    assert(error && strstr(GPTR(error->data), "offline cache only"));
    void_com_release(&error->com);
    void_gfree(text); void_gfree(entry); void_gfree(profile); void_gfree(outputs);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter(); add_system_pseudos();
    mkdir("/data/local/tmp/ut3-d3dx-test", 0775);
    assert(void_files_init("/data/local/tmp", "/data/local/tmp/ut3-d3dx-test"));
    uint32_t module = void_module_load("D3DX9_35.DLL");
    assert(module && void_module_handle("d3dx9_35") == module);
    uint32_t check = target(module, "D3DXCheckVersion"), mute = target(module, "D3DXDebugMute");
    uint32_t compile = target(module, "D3DXCompileShader"), disassemble = target(module, "D3DXDisassembleShader");
    assert(check && mute && compile && disassemble);
    assert(!target(module, "D3DXPreprocessShader") && !target(module, "D3DXInventedFunction"));
    const char *aliases[] = {"D3DXCompileShader", "D3DXDisassembleShader"};
    for (unsigned i = 0; i < 2; ++i)
        assert(void_shim_find("d3dx9_35.dll", aliases[i], 0) == void_shim_find("d3dx9_25.dll", aliases[i], 0));
    static const uint32_t d3d[] = {0, 31, 32, 33, 0x80000020, 0x10020, 0x40000020, 0xFFFFFFFF};
    static const uint32_t d3dx[] = {0, 25, 34, 35, 36, 0x80000023, 0xFFFFFFFF};
    for (unsigned i = 0; i < sizeof d3d / sizeof *d3d; ++i)
        for (unsigned j = 0; j < sizeof d3dx / sizeof *d3dx; ++j) {
            uint32_t args[2] = {d3d[i], d3dx[j]};
            void_set_last_error(123);
            assert(call(check, 2, args) == ((d3d[i] & 0x7FFFFFFF) == 32 && d3dx[j] == 35));
            assert(void_get_last_error() == 123);
        }
    static const uint32_t values[] = {0, 1, 1, 2, 0, 0xFFFFFFFF, 0, 0x7FFFFFFF, 0};
    uint32_t previous = 0;
    for (unsigned i = 0; i < sizeof values / sizeof *values; ++i) {
        assert(call(mute, 1, &values[i]) == previous);
        previous = values[i];
    }
    compile_failure(compile);
    assert(debug_messages);
    unsigned before = debug_messages;
    uint32_t flag = 1;
    assert(!call(mute, 1, &flag));
    compile_failure(compile);
    assert(debug_messages == before);
    flag = 0; assert(call(mute, 1, &flag) == 1);

    static const uint32_t mov[] = {0xFFFF0200, 0x02000001, 0x800F0800, 0xA0E40000, 0x0000FFFF};
    uint32_t code = void_galloc(sizeof mov), output = void_galloc(12);
    memcpy(GPTR(code), mov, sizeof mov);
    memset(GPTR(output), 0xA5, 12);
    uint32_t args[4] = {code, 0, 0, output + 4};
    assert(!call(disassemble, 4, args));
    dxbuffer_t *assembly = void_com_host(MEM32(output + 4));
    assert(assembly && strstr(GPTR(assembly->data), "ps_2_0") && strstr(GPTR(assembly->data), "mov"));
    void_com_release(&assembly->com);
    MEM32(code + 8) &= 0x7FFFFFFF; /* Operand tokens must have reserved bit 31 set. */
    MEM32(output + 4) = 0xA5A5A5A5;
    assert(call(disassemble, 4, args) == D3DXERR_INVALIDDATA && !MEM32(output + 4));
    MEM32(code) = 0;
    assert(call(disassemble, 4, args) == D3DXERR_INVALIDDATA && !MEM32(output + 4));
    args[3] = 0;
    assert(call(disassemble, 4, args) == D3DERR_INVALIDCALL);
    assert(MEM32(output) == 0xA5A5A5A5 && MEM32(output + 8) == 0xA5A5A5A5);
    void_gfree(code); void_gfree(output);
    printf("PASS D3DX9_35 native version matrix, raw BOOL mute/output, shared shader ABI, honest cache miss/invalid bytecode, guards and stdcall\n");
    return 0;
}
