/* Actual SM3 parsing/link/upload: a literal terrain LOD table must survive API constants and relinking. */
#include "../android/host/d3d9_shader.c"
#include <assert.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static char cache_directory[] = "/data/local/tmp/ut3-terrain-test/cache-XXXXXX";
const char *void_cache_dir(void) { return cache_directory; }
static d3d_shader *load(const char *path, int pixel, int stripped)
{
    FILE *file = fopen(path, "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END)); long size = ftell(file); assert(size > 8 && size < 65536);
    rewind(file); uint32_t code = void_galloc((uint32_t)size); assert(code);
    assert(fread(GPTR(code), 1, (size_t)size, file) == (size_t)size); fclose(file);
    if (stripped) {
        uint32_t *words = GPTR(code);
        uint32_t read = 1, write = 1;
        while (read < (uint32_t)size / 4) {
            uint32_t token = words[read];
            uint32_t length = (token & 0xffff) == 0xfffe ? 1 + ((token >> 16) & 0x7fff) :
                              token == 0xffff ? 1 : 1 + ((token >> 24) & 15);
            assert(length && read + length <= (uint32_t)size / 4);
            if ((token & 0xffff) != 0xfffe) { memmove(words + write, words + read, length * 4); write += length; }
            read += length;
        }
    }
    d3d_shader *shader = d3d_shader_create(NULL, pixel, code); assert(shader && shader->parse);
    if (stripped) assert(shader->parse_code);
    void_gfree(code); return shader;
}
int main(int argc, char **argv)
{
    assert(argc == 3 || argc == 5); setvbuf(stdout, NULL, _IONBF, 0);
    assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    setenv("VOID_RENDER_THREAD", "0", 1); assert(d3d_gl_init());
    assert(mkdtemp(cache_directory));
    d3d_shader *vertex[2] = {load(argv[1], 0, 0), load(argv[1], 0, 1)}, *ps = load(argv[2], 1, 0);
    int arrays = 0;
    for (int i = 0; i < vertex[0]->parse->uniform_count; ++i) if (vertex[0]->parse->uniforms[i].constant) ++arrays;
    assert(arrays > 0);
    d3d_device device = {0}; device.st.viewport[2] = device.st.viewport[3] = 16;
    float *constants = &device.st.vs_f[0][0];
    for (int i = 0; i < 256 * 4; ++i) constants[i] = -73;
    GLuint vao, vb, texture, fb; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &vb); glBindBuffer(GL_ARRAY_BUFFER, vb);
    const float positions[] = {-1,-1,0,1, 3,-1,0,1, -1,3,0,1};
    glBufferData(GL_ARRAY_BUFFER, sizeof positions, positions, GL_STATIC_DRAW);
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture); glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 16, 16);
    glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,16,16); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    const float ranges[5][2] = {{0,-1},{16384,16384},{8192,8192},{4096,4096},{0,4096}};
    const float distances[] = {0,1024,4096,6144,8192,16384,24576,32768,65536};
    unsigned checked = 0;
    for (int stripped = 0; stripped < 2; ++stripped) for (int relink = 0; relink < 2; ++relink) {
        d3d_shader *vs = vertex[stripped];
        program_t *p = link_program(vs, ps, 0, 0, 0, 0, 9); assert(p); current_program = p;
        int position = -1, terrain = -1;
        for (int i = 0; i < p->attr_count; ++i) {
            if (p->attr_usage[i] == MOJOSHADER_USAGE_POSITION) position = i;
            if (p->attr_usage[i] == MOJOSHADER_USAGE_TEXCOORD) terrain = i;
        }
        assert(position >= 0 && terrain >= 0);
        glEnableVertexAttribArray((GLuint)position); glVertexAttribPointer((GLuint)position,4,GL_FLOAT,GL_FALSE,16,0);
        glDisableVertexAttribArray((GLuint)terrain);
        for (int level = 0; level < 5; ++level) for (unsigned d = 0; d < sizeof distances/sizeof distances[0]; ++d) {
            device.st.vs_f[20][0] = distances[d]; ++d3d_constant_generation;
            d3d_shader_upload(&device,0,0,8,0,0);
            glVertexAttrib4f((GLuint)terrain,(float)level,.2f,.8f,0);
            glDrawArrays(GL_TRIANGLES,0,3);
            unsigned char actual[4]; glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
            float morph = fminf(1,fmaxf(0,(distances[d]-ranges[level][0])/ranges[level][1]));
            float expected[4] = {morph,.2f+.6f*morph,ranges[level][0]/16384,1};
            for (int c = 0; c < 4; ++c) if (abs((int)actual[c]-(int)lrintf(expected[c]*255)) > 1) {
                printf("FAIL stripped=%d level=%d distance=%g channel=%d expected=%g actual=%u\n",stripped,level,distances[d],c,expected[c]*255,actual[c]);
                return 1;
            }
            assert(glGetError() == GL_NO_ERROR); ++checked;
        }
        current_program = NULL; destroy_program(p);
    }
    assert(void_perf_calls(VOID_PERF_PROGRAM_LINK) >= 1 && void_perf_calls(VOID_PERF_PROGRAM_CACHE) >= 2);
    if (argc == 5) {
        d3d_shader *original = load(argv[3], 0, 0), *flat = load(argv[4], 1, 0);
        program_t *p = link_program(original, flat, 0, 0, 0, 0, 9); assert(p); current_program = p;
        ++d3d_constant_generation; d3d_shader_upload(&device,0,0,8,0,0);
        unsigned literals = 0;
        for (int i = 0; i < original->parse->uniform_count; ++i) {
            const MOJOSHADER_uniform *u = original->parse->uniforms + i;
            if (!u->constant) continue;
            for (int c = 0; c < original->parse->constant_count; ++c) {
                const MOJOSHADER_constant *value = original->parse->constants + c;
                if (value->type != MOJOSHADER_UNIFORM_FLOAT || value->index < u->index ||
                    value->index >= u->index + u->array_count) continue;
                char name[128]; snprintf(name,sizeof name,"%s[%d]",u->name,value->index-u->index);
                GLint location = glGetUniformLocation(p->gl,name); assert(location >= 0);
                float actual[4]; glGetUniformfv(p->gl,location,actual);
                assert(!memcmp(actual,value->value.f,sizeof actual)); ++literals;
            }
        }
        assert(literals > 0 && glGetError() == GL_NO_ERROR);
        printf("PASS original retail terrain shader: %u literal vectors preserved after API constant upload\n",literals);
        current_program = NULL; destroy_program(p);
    }
    printf("PASS %u GPU terrain morph cases: literal and stripped CTAB arrays, all five LOD levels, near/far distances, poisoned API registers, cold link and binary-cache reload\n",checked);
    return 0;
}
