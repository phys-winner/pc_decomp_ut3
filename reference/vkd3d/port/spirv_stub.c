/* The UT3 port compiles HLSL to Direct3D 9 bytecode only. The SPIR-V backend (spirv.c) needs
 * SPIRV-Headers and a generated grammar; it is left out and reports that it is unavailable. */
#include "vkd3d_shader_private.h"

int spirv_compile(struct vsir_program *program, uint64_t config_flags,
        const struct vkd3d_shader_compile_info *compile_info,
        struct vkd3d_shader_code *out, struct vkd3d_shader_message_context *message_context)
{
    (void)program; (void)config_flags; (void)compile_info; (void)out;
    vkd3d_shader_error(message_context, NULL, VKD3D_SHADER_ERROR_SPV_NOT_IMPLEMENTED,
            "SPIR-V output is not built into the UT3 Android port.");
    return VKD3D_ERROR_NOT_IMPLEMENTED;
}
