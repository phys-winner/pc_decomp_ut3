/* Pixel oracle for packed block rows, odd mip sizes and padded source pitch. */
#include "../android/host/d3d9_format.c"
#include <assert.h>

static uint32_t seed = 1234567;
static uint8_t random_byte(void) { seed = seed * 1664525u + 1013904223u; return seed >> 24; }

int main(void)
{
    const int convs[] = { CONV_DXT1, CONV_DXT3, CONV_DXT5, CONV_ATI1, CONV_ATI2 };
    unsigned checked = 0;
    for (unsigned c = 0; c < 5; ++c) for (uint32_t w = 1; w <= 33; ++w) for (uint32_t h = 1; h <= 17; ++h) {
        uint32_t channels = c < 3 ? 4 : c == 3 ? 1 : 2, block = c == 0 || c == 3 ? 8 : 16;
        uint32_t pitch = ((w + 3) / 4 + 1) * block;
        size_t raw_size = (size_t)pitch * ((h + 3) / 4), size = (size_t)w * h * channels;
        uint8_t *raw = malloc(raw_size), *out = malloc(size + 32);
        assert(raw && out);
        for (size_t i = 0; i < raw_size; ++i) raw[i] = random_byte();
        memset(out, 0xa5, size + 32);
        decode_blocks(convs[c], raw, w, h, pitch, out, channels);
        for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
            const uint8_t *b = raw + (size_t)(y / 4) * pitch + (x / 4) * block;
            unsigned at = y % 4 * 4 + x % 4;
            uint8_t expected[16][4], alpha[16], green[16];
            if (c < 3) {
                decode_color_block(b + (c ? 8 : 0), expected, c != 0);
                if (c == 1) expected[at][3] = ((b[at / 2] >> (4 * (at & 1))) & 15) * 17;
                if (c == 2) { decode_alpha_block(b, alpha); expected[at][3] = alpha[at]; }
            } else {
                decode_alpha_block(b, alpha);
                expected[at][0] = alpha[at];
                if (c == 4) {
                    decode_alpha_block(b, green); decode_alpha_block(b + 8, alpha);
                    expected[at][0] = alpha[at]; expected[at][1] = green[at];
                }
            }
            assert(!memcmp(out + ((size_t)y * w + x) * channels, expected[at], channels));
        }
        for (size_t i = size; i < size + 32; ++i) assert(out[i] == 0xa5);
        free(raw); free(out); ++checked;
    }
    printf("PASS %u texture decodes: DXT1/3/5 and ATI1/2, transparency, odd mips, padded pitch, pixel coordinates and output guards\n", checked);
    return 0;
}
