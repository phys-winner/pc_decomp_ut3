/* Offline-only preflight of real UT3 shaders through the production parser
 * and source patching paths. It creates no GL context or shader/program. */
#include "host.h"
#include <stdarg.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
static char last_shader_log[2048];
static void shader_test_log(const char *format, ...)
{
    va_list ap; va_start(ap, format);
    vsnprintf(last_shader_log, sizeof last_shader_log, format, ap);
    va_end(ap);
}
#define void_log shader_test_log
#include "../android/host/d3d9_shader.c"
#undef void_log

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

typedef struct { char text[1024]; uint32_t count, first_index, position; } failure_group;
static failure_group failures[64];
static unsigned failure_groups, ungrouped_failures;

static void record_failure(const char *text, uint32_t index, uint32_t position)
{
    unsigned group;
    for (group = 0; group < failure_groups; ++group)
        if (!strcmp(text, failures[group].text)) break;
    if (group == failure_groups) {
        if (failure_groups == sizeof failures / sizeof *failures) { ++ungrouped_failures; return; }
        ++failure_groups;
        snprintf(failures[group].text, sizeof failures[group].text, "%s", text);
        failures[group].first_index = index;
        failures[group].position = position;
    }
    ++failures[group].count;
}

static void json_string(FILE *file, const char *text)
{
    fputc('"', file);
    for (const unsigned char *s = (const unsigned char *)text; *s; ++s) {
        if (*s == '"' || *s == '\\') fprintf(file, "\\%c", *s);
        else if (*s < 32) fprintf(file, "\\u%04x", *s);
        else fputc(*s, file);
    }
    fputc('"', file);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter();
    FILE *corpus = fopen(argv[1], "rb"); assert(corpus);
    uint32_t count; assert(fread(&count, 4, 1, corpus) == 1 && count == 27892);
    uint32_t passed = 0, failed = 0, framing_failed = 0, vs = 0, ps = 0;
    uint32_t swizzled_passed = 0, swizzled_failed = 0, synthetic_ctab = 0;
    uint32_t max_instructions = 0, max_samplers = 0, max_attributes = 0, max_uniforms = 0;
    uint64_t bytes = 0, glsl_bytes = 0;
    struct timespec start, end; clock_gettime(CLOCK_MONOTONIC, &start);
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t size; assert(fread(&size, 4, 1, corpus) == 1 && size >= 8 && size < 1048576 && !(size & 3));
        uint32_t code = void_galloc(size); assert(code);
        assert(fread(GPTR(code), 1, size, corpus) == size);
        uint32_t version = MEM32(code); assert(version == 0xFFFF0300 || version == 0xFFFE0300);
        int pixel = version == 0xFFFF0300;
        if (pixel) ++ps; else ++vs;
        bytes += size;
        if (d3d_shader_bytecode_size(code) != size) ++framing_failed;
        last_shader_log[0] = 0;
        d3d_shader *shader = d3d_shader_create(NULL, pixel, code);
        if (!shader) {
            ++failed;
            const MOJOSHADER_parseData *pd = MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSLES3, "main",
                GPTR(code), size, NULL, 0, NULL, 0, NULL, NULL, NULL);
            const char *message = pd && pd->error_count ? pd->errors[0].error : last_shader_log;
            record_failure(message && *message ? message : "No parser diagnostic", index,
                pd && pd->error_count ? (uint32_t)pd->errors[0].error_position : 0);
            if (pd) MOJOSHADER_freeParseData(pd);
        } else {
            ++passed;
            assert(shader->parse && !shader->parse->error_count && shader->source && *shader->source);
            assert(!strncmp(shader->source, "#version 310 es", 15) && strstr(shader->source, "void main()"));
            if (shader->parse_code) ++synthetic_ctab;
            glsl_bytes += strlen(shader->source);
            if ((uint32_t)shader->parse->instruction_count > max_instructions) max_instructions = shader->parse->instruction_count;
            if ((uint32_t)shader->parse->sampler_count > max_samplers) max_samplers = shader->parse->sampler_count;
            if ((uint32_t)shader->parse->attribute_count > max_attributes) max_attributes = shader->parse->attribute_count;
            if ((uint32_t)shader->parse->uniform_count > max_uniforms) max_uniforms = shader->parse->uniform_count;
            if (!pixel) {
                char *variant = vertex_source(shader, 0xFFFF);
                if (variant) { ++swizzled_passed; free(variant); }
                else { ++swizzled_failed; record_failure("Production BGRA-input vertex variant failed", index, 0); }
            }
            shader_destroy(&shader->com);
        }
        void_gfree(code);
        if ((index + 1) % 4000 == 0)
            printf("progress %u/%u pass=%u failed=%u\n", index + 1, count, passed, failed);
    }
    assert(fgetc(corpus) == EOF); fclose(corpus);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double seconds = (double)(end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    FILE *result = fopen(argv[2], "w"); assert(result);
    fprintf(result, "{\n  \"streams\":%u,\"pixel\":%u,\"vertex\":%u,\"bytecode_bytes\":%llu,\n"
        "  \"passed\":%u,\"failed\":%u,\"framing_size_mismatches\":%u,\n"
        "  \"bgra_vertex_passed\":%u,\"bgra_vertex_failed\":%u,\"synthetic_ctab\":%u,\n"
        "  \"max_instructions\":%u,\"max_samplers\":%u,\"max_attributes\":%u,\"max_uniforms\":%u,\n"
        "  \"glsl_bytes\":%llu,\"seconds\":%.6f,\"ungrouped_failures\":%u,\n  \"failures\":[",
        count, ps, vs, (unsigned long long)bytes, passed, failed, framing_failed,
        swizzled_passed, swizzled_failed, synthetic_ctab, max_instructions, max_samplers, max_attributes, max_uniforms,
        (unsigned long long)glsl_bytes, seconds, ungrouped_failures);
    for (unsigned i = 0; i < failure_groups; ++i) {
        if (i) fputc(',', result);
        fprintf(result, "\n    {\"count\":%u,\"first_index\":%u,\"error_position\":%u,\"message\":",
            failures[i].count, failures[i].first_index, failures[i].position);
        json_string(result, failures[i].text); fputc('}', result);
    }
    fprintf(result, "\n  ],\n  \"scope\":\"Production GLSLES3 parsing, bytecode framing, source patches and BGRA vertex variant; no GL compile/link/draw performed\"\n}\n");
    fclose(result);
    printf("DONE streams=%u pass=%u failed=%u framing_mismatch=%u BGRA=%u/%u groups=%u seconds=%.3f\n",
        count, passed, failed, framing_failed, swizzled_passed, swizzled_passed + swizzled_failed, failure_groups, seconds);
    return failed || swizzled_failed || framing_failed ? 1 : 0;
}
