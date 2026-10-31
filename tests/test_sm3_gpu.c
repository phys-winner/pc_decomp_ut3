/* Representative original SM3 shaders through production GLES compilation. */
#include "host.h"
#include <assert.h>
#include <unistd.h>
#include "../android/host/d3d9_shader.c"

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static void json_string(FILE *out, const char *text)
{
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '"' || *p == '\\') fprintf(out, "\\%c", *p);
        else if (*p < 32) fprintf(out, "\\u%04x", *p);
        else fputc(*p, out);
    }
    fputc('"', out);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter();
    assert(d3d_gl_init());
    FILE *corpus = fopen(argv[1], "rb"); assert(corpus);
    uint32_t count; assert(fread(&count, 4, 1, corpus) == 1 && count == 27892);
    uint8_t *selected = calloc(count, 1); assert(selected);
    uint32_t best_value[2][4] = {{0}}, best_index[2][4] = {{0}};
    uint32_t stage_count[2] = {0};
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t size; assert(fread(&size, 4, 1, corpus) == 1 && size >= 8 && size < 1048576 && !(size & 3));
        void *code = malloc(size); assert(code && fread(code, 1, size, corpus) == size);
        uint32_t version; memcpy(&version, code, 4);
        assert(version == 0xFFFF0300 || version == 0xFFFE0300);
        unsigned pixel = version == 0xFFFF0300;
        const MOJOSHADER_parseData *pd = MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSLES3, "main", code, size,
            NULL, 0, NULL, 0, NULL, NULL, NULL);
        assert(pd && !pd->error_count);
        const uint32_t metrics[4] = {pd->instruction_count, pd->sampler_count, pd->attribute_count, pd->uniform_count};
        for (unsigned metric = 0; metric < 4; ++metric)
            if (!stage_count[pixel] || metrics[metric] > best_value[pixel][metric]) {
                best_value[pixel][metric] = metrics[metric];
                best_index[pixel][metric] = index;
            }
        /* Each stage is sampled across its serialized order, including sparse VS. */
        if (stage_count[pixel] % (pixel ? 837 : 35) == 0) selected[index] = 1;
        ++stage_count[pixel];
        MOJOSHADER_freeParseData(pd); free(code);
    }
    assert(fgetc(corpus) == EOF);
    for (unsigned stage = 0; stage < 2; ++stage)
        for (unsigned metric = 0; metric < 4; ++metric) selected[best_index[stage][metric]] = 1;
    rewind(corpus); assert(fread(&count, 4, 1, corpus) == 1);
    FILE *out = fopen(argv[2], "w"); assert(out);
    fprintf(out, "{\n\"scope\":\"Production shader parsing, source patches and GLES driver compile; no link/draw\",\n\"vendor\":");
    json_string(out, (const char *)glGetString(GL_VENDOR));
    fprintf(out, ",\n\"renderer\":"); json_string(out, (const char *)glGetString(GL_RENDERER));
    fprintf(out, ",\n\"version\":"); json_string(out, (const char *)glGetString(GL_VERSION));
    fprintf(out, ",\n\"selection\":\"Every 35th vertex and 837th pixel record plus maxima of instructions/samplers/attributes/uniforms per stage\",\n\"shaders\":[");
    unsigned sampled = 0, passed = 0, failed = 0, bgra_passed = 0, bgra_failed = 0;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t size; assert(fread(&size, 4, 1, corpus) == 1);
        if (!selected[index]) { assert(!fseek(corpus, size, SEEK_CUR)); continue; }
        uint32_t code = void_galloc(size); assert(code);
        assert(fread(GPTR(code), 1, size, corpus) == size);
        int pixel = MEM32(code) == 0xFFFF0300;
        assert(d3d_shader_bytecode_size(code) == size);
        d3d_shader *shader = d3d_shader_create(NULL, pixel, code); assert(shader && !shader->parse_code);
        GLuint gl = compile(pixel ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER, shader->source);
        if (gl) { ++passed; glDeleteShader(gl); } else ++failed;
        GLuint bgra = pixel ? 0 : vertex_variant(shader, 0xFFFF);
        if (!pixel) {
            if (bgra) { ++bgra_passed; glDeleteShader(bgra); } else ++bgra_failed;
        }
        if (sampled++) fputc(',', out);
        fprintf(out, "\n{\"index\":%u,\"pixel\":%s,\"bytes\":%u,\"instructions\":%u,\"samplers\":%u,\"attributes\":%u,\"uniforms\":%u,\"compiled\":%s,\"bgra_compiled\":%s}",
            index, pixel ? "true" : "false", size, shader->parse->instruction_count, shader->parse->sampler_count,
            shader->parse->attribute_count, shader->parse->uniform_count, gl ? "true" : "false",
            pixel ? "null" : bgra ? "true" : "false");
        printf("shader %u %s compile=%u BGRA=%u\n", index, pixel ? "PS" : "VS", gl != 0, bgra != 0);
        shader_destroy(&shader->com); void_gfree(code);
    }
    fprintf(out, "\n],\"sampled\":%u,\"passed\":%u,\"failed\":%u,\"bgra_passed\":%u,\"bgra_failed\":%u}\n",
        sampled, passed, failed, bgra_passed, bgra_failed);
    fclose(out); fclose(corpus); free(selected);
    assert(sampled >= 32 && sampled <= 128);
    printf("DONE sampled=%u pass=%u fail=%u BGRA=%u fail=%u\n", sampled, passed, failed, bgra_passed, bgra_failed);
    return failed || bgra_failed;
}
