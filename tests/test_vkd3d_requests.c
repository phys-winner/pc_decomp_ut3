/* On-device check of the port's HLSL compiler (android/host/hlsl_vkd3d.c, vkd3d-shader) against the
 * game's own: every recorded D3DXCompileShader request of a folder is compiled on the phone and
 * compared with the bytecode d3dx9_35.dll made from the same request on Windows (same key):
 * success, the constant table's names/register sets/sizes (UE3 binds parameters through it) and
 * MojoShader's parse (the GLES renderer's translator).
 *
 * usage: test_vkd3d_requests REQUEST_DIR REFERENCE_DIR [VERBOSE_ERRORS [SHARD COUNT [TIMES_FILE]]]
 * SHARD/COUNT split the requests for parallel runs; TIMES_FILE gets "key profile ms result" lines. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../android/host/hlsl_vkd3d.c"
#include "mojoshader.h"

typedef struct { char name[128]; unsigned set, count; } constant;

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    unsigned char *data = malloc((size_t)n + 1);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
    if (data) data[n] = 0;
    fclose(f);
    *size = (size_t)n;
    return data;
}

/* The CTAB comment's constants; -1 without one. */
static int constants(const unsigned char *code, size_t size, constant *out, int cap)
{
    const uint32_t *t = (const uint32_t *)code;
    size_t words = size / 4;
    for (size_t i = 1; i < words; ++i) {
        if ((t[i] & 0xFFFF) != 0xFFFE) {
            if ((t[i] & 0xFFFF) == 0xFFFF) break;
            continue;
        }
        uint32_t length = (t[i] >> 16) & 0x7FFF;
        if (i + 1 + length > words) break;
        if (length >= 8 && t[i + 1] == 0x42415443u) {       /* 'CTAB' */
            const unsigned char *base = (const unsigned char *)&t[i + 2];
            size_t bytes = (length - 1) * 4;
            uint32_t count, info;
            memcpy(&count, base + 12, 4);
            memcpy(&info, base + 16, 4);
            int n = 0;
            for (uint32_t c = 0; c < count && n < cap && info + 20 * (c + 1) <= bytes; ++c) {
                const unsigned char *e = base + info + 20 * c;
                uint32_t name;
                uint16_t set, reg_count;
                memcpy(&name, e, 4);
                memcpy(&set, e + 4, 2);
                memcpy(&reg_count, e + 8, 2);
                snprintf(out[n].name, sizeof out[n].name, "%s", name < bytes ? (const char *)base + name : "?");
                out[n].set = set;
                out[n].count = reg_count;
                ++n;
            }
            return n;
        }
        i += length;
    }
    return -1;
}

static int compare_constant(const void *a, const void *b) { return strcmp(((const constant *)a)->name, ((const constant *)b)->name); }

/* --bins DIR: MojoShader's GLSL and D3D (disassembly) profiles over compiled bytecode files, such
 * as <data>/shadercache/<key>.vkd3d-*.bin from a game run. */
static int check_bins(const char *folder)
{
    DIR *dir = opendir(folder);
    if (!dir) { perror(folder); return 2; }
    unsigned total = 0, glsl_failed = 0, disasm_failed = 0;
    for (struct dirent *e; (e = readdir(dir));) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 4, ".bin")) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", folder, e->d_name);
        size_t size;
        unsigned char *code = read_file(path, &size);
        if (!code) continue;
        ++total;
        for (int profile = 0; profile < 2; ++profile) {
            const MOJOSHADER_parseData *pd = MOJOSHADER_parse(profile ? MOJOSHADER_PROFILE_D3D : MOJOSHADER_PROFILE_GLSL,
                "main", code, (unsigned)size, NULL, 0, NULL, 0, NULL, NULL, NULL);
            if (!pd || pd->error_count || !pd->output) {
                ++*(profile ? &disasm_failed : &glsl_failed);
                printf("%s %s: %s\n", profile ? "DISASM" : "MOJO", e->d_name,
                    pd && pd->error_count ? pd->errors[0].error : "no output");
            } else if (!profile && getenv("VKD3D_TEST_GLSL")) {
                snprintf(path, sizeof path, "%s/%.16s.%s", getenv("VKD3D_TEST_GLSL"), e->d_name,
                    (code[2] | code[3] << 8) == 0xFFFF ? "frag" : "vert");
                FILE *f = fopen(path, "wb");
                if (f) { fwrite(pd->output, 1, (size_t)pd->output_len, f); fclose(f); }
            }
            if (pd) MOJOSHADER_freeParseData(pd);
        }
        free(code);
    }
    closedir(dir);
    printf("BINS total %u glsl_failed %u disasm_failed %u\n", total, glsl_failed, disasm_failed);
    return glsl_failed || disasm_failed;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--bins")) return check_bins(argv[2]);
    if (argc < 3) { fprintf(stderr, "usage: %s REQUEST_DIR REFERENCE_DIR [VERBOSE] | --bins DIR\n", argv[0]); return 2; }
    int verbose = argc > 3 ? atoi(argv[3]) : 3;
    unsigned shard = argc > 5 ? (unsigned)atoi(argv[4]) : 0, shards = argc > 5 ? (unsigned)atoi(argv[5]) : 1;
    FILE *times = argc > 6 ? fopen(argv[6], "w") : NULL;
    unsigned ordinal = 0;
    DIR *dir = opendir(argv[1]);
    if (!dir) { perror(argv[1]); return 2; }
    unsigned total = 0, compiled = 0, failed = 0, table_same = 0, table_differs = 0, table_risky = 0, mojo_ok = 0, skipped = 0;
    unsigned disasm_failed = 0;
    double seconds = 0, slowest = 0;
    for (struct dirent *e; (e = readdir(dir));) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 4, ".req")) continue;
        if (ordinal++ % shards != shard) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", argv[1], e->d_name);
        size_t size;
        unsigned char *req = read_file(path, &size);
        if (!req || memcmp(req, "UT3SHADERREQ 2\n", 15)) { free(req); ++skipped; continue; }
        /* key=, function=, profile=, flags=, macros=N, include SIZE NAME..., source=N */
        char *p = (char *)req + 15, key[64] = "", function[128] = "", profile[32] = "";
        unsigned flags = 0, macro_bytes = 0;
        sscanf(p, "key=%63s", key); p = strchr(p, '\n') + 1;
        sscanf(p, "function=%127s", function); p = strchr(p, '\n') + 1;
        sscanf(p, "profile=%31s", profile); p = strchr(p, '\n') + 1;
        sscanf(p, "flags=%u", &flags); p = strchr(p, '\n') + 1;
        sscanf(p, "macros=%u", &macro_bytes); p = strchr(p, '\n') + 1;
        if (function[0] == '@') { free(req); ++skipped; continue; }
        char *macro_text = malloc(macro_bytes + 1);
        memcpy(macro_text, p, macro_bytes); macro_text[macro_bytes] = 0; p += macro_bytes;
        const char *names[256], *values[256];
        unsigned macro_count = 0;
        for (char *line = macro_text; *line && macro_count < 256;) {
            char *end = strchr(line, '\n');
            if (end) *end = 0;
            char *eq = strchr(line, '=');
            if (eq) { *eq = 0; names[macro_count] = line; values[macro_count++] = eq + 1; }
            if (!end) break;
            line = end + 1;
        }
        const char *include_names[64];
        const unsigned char *include_data[64];
        uint32_t include_sizes[64];
        char include_name_store[64][256];
        unsigned include_count = 0;
        while (!strncmp(p, "include ", 8) && include_count < 64) {
            unsigned bytes = 0;
            char name[256];
            sscanf(p, "include %u %255[^\n]", &bytes, name);
            p = strchr(p, '\n') + 1;
            snprintf(include_name_store[include_count], 256, "%s", name);
            include_names[include_count] = include_name_store[include_count];
            include_data[include_count] = (const unsigned char *)p;
            include_sizes[include_count++] = bytes;
            p += bytes;
        }
        unsigned source_bytes = 0;
        sscanf(p, "source=%u", &source_bytes);
        p = strchr(p, '\n') + 1;
        ++total;
        void *code; size_t code_size; char *messages;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int ok = void_hlsl_compile(p, source_bytes, function, profile, flags, names, values, macro_count,
            include_names, include_data, include_sizes, include_count, &code, &code_size, &messages);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double took = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
        seconds += took;
        if (took > slowest) slowest = took;
        if (times) { fprintf(times, "%s %s %.1f %s %u\n", key, profile, took * 1000.0, ok ? "ok" : "fail", source_bytes); fflush(times); }
        if (!ok) {
            if (failed++ < (unsigned)verbose) printf("FAIL %s %s: %.600s\n", key, profile, messages ? messages : "(no messages)");
            free(messages); free(macro_text); free(req);
            continue;
        }
        ++compiled;
        const MOJOSHADER_parseData *pd = MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSL, "main", code, (unsigned)code_size,
            NULL, 0, NULL, 0, NULL, NULL, NULL);
        if (pd && !pd->error_count) {
            ++mojo_ok;
            const char *glsl = getenv("VKD3D_TEST_GLSL");     /* the translation, for glslangValidator */
            if (glsl && pd->output) {
                snprintf(path, sizeof path, "%s/%s.%s", glsl, key, profile[0] == 'p' ? "frag" : "vert");
                FILE *f = fopen(path, "wb");
                if (f) { fwrite(pd->output, 1, (size_t)pd->output_len, f); fclose(f); }
            }
        }
        else if (verbose) printf("MOJO %s: %s\n", key, pd && pd->error_count ? pd->errors[0].error : "parse failed");
        if (pd) MOJOSHADER_freeParseData(pd);
        /* UE3 disassembles every shader it compiles and stops with appError when D3DXDisassembleShader
         * fails (D3D9ShaderCompiler.cpp); the port's fallback disassembler is MojoShader's D3D profile. */
        pd = MOJOSHADER_parse(MOJOSHADER_PROFILE_D3D, "main", code, (unsigned)code_size, NULL, 0, NULL, 0, NULL, NULL, NULL);
        if (!pd || pd->error_count || !pd->output) {
            ++disasm_failed;
            if (verbose) printf("DISASM %s: %s\n", key, pd && pd->error_count ? pd->errors[0].error : "no output");
        }
        if (pd) MOJOSHADER_freeParseData(pd);
        snprintf(path, sizeof path, "%s/%s.bin", argv[2], key);
        size_t ref_size;
        unsigned char *ref = read_file(path, &ref_size);
        if (ref) {
            /* UE3 binds parameters by name: an extra one in vkd3d's table is harmless (d3dx9 folded
             * its use away), a missing one breaks a non-optional binding, a different register set
             * or size changes how the game writes it. */
            static constant a[512], b[512];
            int na = constants(code, code_size, a, 512), nb = constants(ref, ref_size, b, 512);
            if (na < 0) na = 0;
            if (nb < 0) nb = 0;
            qsort(a, (size_t)na, sizeof *a, compare_constant);
            qsort(b, (size_t)nb, sizeof *b, compare_constant);
            char diff[2048];
            size_t used = 0;
            int missing = 0, changed = 0, extra = 0;
            for (int i = 0, j = 0; i < na || j < nb;) {
                int order = i == na ? 1 : j == nb ? -1 : strcmp(a[i].name, b[j].name);
                if (order < 0) { ++extra; used += (size_t)snprintf(diff + used, used < sizeof diff ? sizeof diff - used : 0, " +%s", a[i].name); ++i; }
                else if (order > 0) { ++missing; used += (size_t)snprintf(diff + used, used < sizeof diff ? sizeof diff - used : 0, " -%s", b[j].name); ++j; }
                else {
                    if (a[i].set != b[j].set || a[i].count != b[j].count) {
                        ++changed;
                        used += (size_t)snprintf(diff + used, used < sizeof diff ? sizeof diff - used : 0, " ~%s(vkd3d %u/%u, d3dx9 %u/%u)",
                            a[i].name, a[i].set, a[i].count, b[j].set, b[j].count);
                    }
                    ++i; ++j;
                }
            }
            if (!missing && !changed && !extra) ++table_same;
            else {
                ++table_differs;
                if (missing || changed) ++table_risky;
                if (verbose) printf("CTAB %s %s: %d extra, %d missing, %d changed:%.1500s\n", key, profile, extra, missing, changed, diff);
            }
            free(ref);
        }
        const char *save = getenv("VKD3D_TEST_OUT");
        if (save) {
            snprintf(path, sizeof path, "%s/%s.bin", save, key);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(code, 1, code_size, f); fclose(f); }
        }
        free(code); free(messages); free(macro_text); free(req);
    }
    closedir(dir);
    printf("SUMMARY total %u compiled %u failed %u mojoshader_ok %u ctab_same %u ctab_differs %u skipped %u ctab_risky %u disasm_failed %u; %.1f ms average, %.1f ms slowest\n",
        total, compiled, failed, mojo_ok, table_same, table_differs, skipped, table_risky, disasm_failed, total ? 1000.0 * seconds / total : 0, 1000.0 * slowest);
    return failed ? 1 : 0;
}
