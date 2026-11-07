/* HLSL -> Direct3D 9 shader bytecode on the device, with vkd3d-shader (reference/vkd3d, Wine's
 * compiler, LGPL-2.1+): D3DXCompileShader for shaders no cache holds, so any mod's materials
 * compile on Android as they do on Windows. d3dx9.c calls it with the include files the game's
 * ID3DXInclude served and stores the result in the shader cache. */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "vkd3d_shader.h"           /* reference/vkd3d/include (build_android.py adds it for this file) */

typedef struct {
    const char *const *names;
    const unsigned char *const *data;
    const uint32_t *sizes;
    unsigned count;
} hlsl_includes;

/* d3dx9 knows the [isolate] attribute of a statement block ("optimise this code independently of
 * the surrounding code"); vkd3d's grammar takes attributes only before if and loops and rejects
 * the block. UE3's vertex factories put one before the position transform (GpuSkinVertexFactory,
 * LocalVertexFactory: "[isolate] { Result = MulMatrix(LocalToWorld, ...); }"), so every material
 * on such a mesh failed. The attribute only steers fxc's optimiser; it is blanked out (spaces keep
 * the line numbers of messages). Returns a malloc'd copy when something was blanked, else NULL. */
static char *hlsl_drop_isolate(const char *text, size_t size)
{
    char *copy = NULL;
    for (size_t i = 0; i < size; ++i) {
        if (text[i] != '[') continue;
        size_t p = i + 1;
        while (p < size && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (size - p < 7 || strncasecmp(text + p, "isolate", 7)) continue;
        p += 7;
        while (p < size && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (p == size || text[p] != ']') continue;
        if (!copy) {
            if (!(copy = malloc(size ? size : 1))) return NULL;
            memcpy(copy, text, size);
        }
        memset(copy + i, ' ', p + 1 - i);
        i = p;
    }
    return copy;
}

static int hlsl_open_include(const char *filename, bool local, const char *parent_data, void *context,
    struct vkd3d_shader_code *out)
{
    (void)local; (void)parent_data;
    const hlsl_includes *includes = (const hlsl_includes *)context;
    for (unsigned i = 0; i < includes->count; ++i)
        if (!strcasecmp(includes->names[i], filename)) {
            char *edited = hlsl_drop_isolate((const char *)includes->data[i], includes->sizes[i]);
            out->code = edited ? (const void *)edited : (const void *)includes->data[i];
            out->size = includes->sizes[i];
            return VKD3D_OK;
        }
    return VKD3D_ERROR_NOT_FOUND;
}

static void hlsl_close_include(const struct vkd3d_shader_code *code, void *context)
{
    const hlsl_includes *includes = (const hlsl_includes *)context;
    for (unsigned i = 0; i < includes->count; ++i)
        if (code->code == (const void *)includes->data[i]) return;
    free((void *)code->code);                   /* a copy hlsl_drop_isolate made */
}

/* D3DXSHADER_* flags that change the result; the optimisation, validation and debug flags do not
 * apply to vkd3d-shader. Returns 1 with *code (malloc) or 0 with *messages (malloc, may be NULL). */
int void_hlsl_compile(const char *source, size_t size, const char *entry, const char *profile, uint32_t flags,
    const char *const *macro_names, const char *const *macro_values, unsigned macro_count,
    const char *const *include_names, const unsigned char *const *include_data, const uint32_t *include_sizes,
    unsigned include_count, void **code, size_t *code_size, char **messages)
{
    *code = NULL; *code_size = 0; *messages = NULL;
    struct vkd3d_shader_macro *macros = macro_count ? calloc(macro_count, sizeof *macros) : NULL;
    if (macro_count && !macros) return 0;
    for (unsigned i = 0; i < macro_count; ++i) { macros[i].name = macro_names[i]; macros[i].value = macro_values[i]; }
    hlsl_includes includes = { include_names, include_data, include_sizes, include_count };
    struct vkd3d_shader_hlsl_source_info hlsl = { VKD3D_SHADER_STRUCTURE_TYPE_HLSL_SOURCE_INFO, NULL, entry, { NULL, 0 }, profile };
    struct vkd3d_shader_preprocess_info preprocess = { VKD3D_SHADER_STRUCTURE_TYPE_PREPROCESS_INFO, &hlsl, macros, macro_count,
        hlsl_open_include, hlsl_close_include, &includes };
    struct vkd3d_shader_compile_option options[3];
    unsigned option_count = 0;
    /* d3dx9 accepts 'half' and 'double' globals in shader model 1-3 (as float). */
    options[option_count++] = (struct vkd3d_shader_compile_option){ VKD3D_SHADER_COMPILE_OPTION_BACKWARD_COMPATIBILITY,
        VKD3D_SHADER_COMPILE_OPTION_BACKCOMPAT_ALLOW_HALF_GLOBALS | VKD3D_SHADER_COMPILE_OPTION_DOUBLE_AS_FLOAT_ALIAS };
    if (flags & 0x8u)              /* D3DXSHADER_PACKMATRIX_ROWMAJOR */
        options[option_count++] = (struct vkd3d_shader_compile_option){ VKD3D_SHADER_COMPILE_OPTION_PACK_MATRIX_ORDER,
            VKD3D_SHADER_COMPILE_OPTION_PACK_MATRIX_ROW_MAJOR };
    else if (flags & 0x10u)        /* D3DXSHADER_PACKMATRIX_COLUMNMAJOR */
        options[option_count++] = (struct vkd3d_shader_compile_option){ VKD3D_SHADER_COMPILE_OPTION_PACK_MATRIX_ORDER,
            VKD3D_SHADER_COMPILE_OPTION_PACK_MATRIX_COLUMN_MAJOR };
    char *edited = hlsl_drop_isolate(source, size);
    struct vkd3d_shader_compile_info info = { VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO, &preprocess,
        { edited ? edited : source, size }, VKD3D_SHADER_SOURCE_HLSL, VKD3D_SHADER_TARGET_D3D_BYTECODE, options,
        option_count, VKD3D_SHADER_LOG_INFO, "D3DXCompileShader" };
    struct vkd3d_shader_code output = { NULL, 0 };
    char *log = NULL;
    int result = vkd3d_shader_compile(&info, &output, &log);
    free(macros);
    free(edited);
    if (result >= 0 && output.code && output.size) {
        *code = malloc(output.size);
        if (*code) { memcpy(*code, output.code, output.size); *code_size = output.size; }
        vkd3d_shader_free_shader_code(&output);
        if (log) vkd3d_shader_free_messages(log);
        return *code != NULL;
    }
    vkd3d_shader_free_shader_code(&output);
    if (log) { *messages = strdup(log); vkd3d_shader_free_messages(log); }
    return 0;
}
