/* DXT decode and ASTC/ETC2 encode reused from The Void android/texconv. */
#include "texconv.h"
#include "tvtx.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

#include "../../reference/astcenc/Source/astcenc.h"
#include "../../reference/etcpak/Decode.hpp"
#include "../../reference/etcpak/ProcessRGB.hpp"

namespace {
uint32_t le32(const uint8_t *p) { return tvtx_le32(p); }
void rgb565(uint32_t c, uint8_t *out)
{
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
    out[1] = static_cast<uint8_t>((g << 2) | (g >> 4));
    out[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
}

void color_block(const uint8_t *block, uint8_t out[16][4], bool four)
{
    uint32_t c0 = block[0] | (block[1] << 8), c1 = block[2] | (block[3] << 8), bits = le32(block + 4);
    uint8_t pal[4][4];
    rgb565(c0, pal[0]);
    rgb565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (c0 > c1 || four) {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = static_cast<uint8_t>((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = static_cast<uint8_t>((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) pal[2][k] = static_cast<uint8_t>((pal[0][k] + pal[1][k]) / 2);
        pal[2][3] = 255;
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
    }
    for (int i = 0; i < 16; ++i) memcpy(out[i], pal[(bits >> (2 * i)) & 3], 4);
}

void alpha_block(const uint8_t *block, uint8_t out[16])
{
    uint32_t a0 = block[0], a1 = block[1];
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    uint8_t pal[8];
    pal[0] = static_cast<uint8_t>(a0);
    pal[1] = static_cast<uint8_t>(a1);
    if (a0 > a1) {
        for (uint32_t i = 1; i < 7; ++i) pal[1 + i] = static_cast<uint8_t>(((7 - i) * a0 + i * a1 + 3) / 7);
    } else {
        for (uint32_t i = 1; i < 5; ++i) pal[1 + i] = static_cast<uint8_t>(((5 - i) * a0 + i * a1 + 2) / 5);
        pal[6] = 0;
        pal[7] = 255;
    }
    for (int i = 0; i < 16; ++i) out[i] = pal[(bits >> (3 * i)) & 7];
}

void decode_dxt(uint32_t fourcc, const uint8_t *src, uint32_t width, uint32_t height, uint8_t *rgba)
{
    uint32_t kind = fourcc >> 24, bw = (width + 3) / 4, bh = (height + 3) / 4, step = kind == '1' ? 8 : 16;
    for (uint32_t by = 0; by < bh; ++by)
        for (uint32_t bx = 0; bx < bw; ++bx) {
            const uint8_t *block = src + (static_cast<size_t>(by) * bw + bx) * step;
            uint8_t texels[16][4], alpha[16];
            if (kind == '1') {
                color_block(block, texels, false);
            } else if (kind == '2' || kind == '3') {
                color_block(block + 8, texels, true);
                for (int i = 0; i < 16; ++i) texels[i][3] = static_cast<uint8_t>(((block[i / 2] >> (4 * (i & 1))) & 15) * 17);
            } else {
                color_block(block + 8, texels, true);
                alpha_block(block, alpha);
                for (int i = 0; i < 16; ++i) texels[i][3] = alpha[i];
            }
            for (uint32_t y = 0; y < 4 && by * 4 + y < height; ++y)
                for (uint32_t x = 0; x < 4 && bx * 4 + x < width; ++x)
                    memcpy(rgba + ((static_cast<size_t>(by) * 4 + y) * width + bx * 4 + x) * 4, texels[y * 4 + x], 4);
        }
}

void level_dims(const tvtx_dds &d, uint32_t level, uint32_t &w, uint32_t &h)
{
    w = d.width >> level ? d.width >> level : 1;
    h = d.height >> level ? d.height >> level : 1;
}

uint64_t dxt_level_bytes(const tvtx_dds &d, uint32_t w, uint32_t h)
{
    return static_cast<uint64_t>((w + 3) / 4) * ((h + 3) / 4) * d.block;
}

/* -------------------------------------------------------------- encoders */

struct AstcContext {
    astcenc_context *context[2] = {nullptr, nullptr};   /* opaque, with alpha */
    ~AstcContext() { for (astcenc_context *c : context) if (c) astcenc_context_free(c); }

    astcenc_context *get(bool opaque)
    {
        astcenc_context *&c = context[opaque ? 0 : 1];
        if (c) return c;
        astcenc_config config;
        /* VOID_TEXCONV_ASTC_QUALITY (a development knob): astcenc's quality, 0..100. */
        const char *knob = getenv("VOID_TEXCONV_ASTC_QUALITY");
        float quality = knob ? static_cast<float>(atof(knob)) : ASTCENC_PRE_FAST;
        unsigned flags = opaque ? 0u : ASTCENC_FLG_USE_ALPHA_WEIGHT;
        if (astcenc_config_init(ASTCENC_PRF_LDR, 4, 4, 1, quality, flags, &config) != ASTCENC_SUCCESS) return nullptr;
        if (astcenc_context_alloc(&config, 1, &c, nullptr) != ASTCENC_SUCCESS) c = nullptr;
        return c;
    }
};

thread_local AstcContext astc;

bool encode_level(uint32_t gl_format, const uint8_t *rgba, uint32_t w, uint32_t h, bool opaque, uint8_t *out)
{
    if (gl_format == TVTX_GL_ETC2_RGB8 || gl_format == TVTX_GL_ETC2_RGBA8) {
        /* etcpak: whole 4x4 blocks of BGRA words, rows of the padded width; edges repeat. */
        uint32_t pw = (w + 3) & ~3u, ph = (h + 3) & ~3u;
        std::vector<uint32_t> bgra(static_cast<size_t>(pw) * ph);
        for (uint32_t y = 0; y < ph; ++y)
            for (uint32_t x = 0; x < pw; ++x) {
                const uint8_t *p = rgba + (static_cast<size_t>(std::min(y, h - 1)) * w + std::min(x, w - 1)) * 4;
                bgra[static_cast<size_t>(y) * pw + x] = static_cast<uint32_t>(p[2]) | (static_cast<uint32_t>(p[1]) << 8) |
                                                      (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
            }
        uint32_t blocks = (pw / 4) * (ph / 4);
        /* etcpak's heuristics choose the T, H and planar modes besides ETC1's: real
         * textures 24..43 dB against 21..42 without them. */
        if (gl_format == TVTX_GL_ETC2_RGB8) CompressEtc2Rgb(bgra.data(), reinterpret_cast<uint64_t *>(out), blocks, pw, true);
        else CompressEtc2Rgba(bgra.data(), reinterpret_cast<uint64_t *>(out), blocks, pw, true);
        return true;
    }
    astcenc_context *context = astc.get(opaque);
    if (!context) return false;
    astcenc_image image{};
    image.dim_x = w;
    image.dim_y = h;
    image.dim_z = 1;
    image.data_type = ASTCENC_TYPE_U8;
    void *slices[] = {const_cast<uint8_t *>(rgba)};
    image.data = slices;
    /* An opaque texture spends no bits on alpha. */
    const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, opaque ? ASTCENC_SWZ_1 : ASTCENC_SWZ_A};
    astcenc_error status = astcenc_compress_image(context, &image, &swizzle, out, tvtx_level_bytes(gl_format, w, h), 0);
    astcenc_compress_reset(context);
    return status == ASTCENC_SUCCESS;
}

/* One DXT DDS into its payload. 0 done, -1 not convertible or the encoder failed. */
int convert_dds(const uint8_t *dds, uint64_t size, int family, std::vector<uint8_t> &payload, uint32_t &gl_format,
                uint32_t &flags)
{
    tvtx_dds d;
    if (!tvtx_dds_bytes(dds, size, &d)) return -1;
    std::vector<std::vector<uint8_t>> levels(static_cast<size_t>(d.faces) * d.mips);
    bool opaque = true;
    const uint8_t *src = dds + 128;
    for (uint32_t f = 0; f < d.faces; ++f)
        for (uint32_t l = 0; l < d.mips; ++l) {
            uint32_t w, h;
            level_dims(d, l, w, h);
            std::vector<uint8_t> &level = levels[static_cast<size_t>(f) * d.mips + l];
            level.resize(static_cast<size_t>(w) * h * 4);
            decode_dxt(d.fourcc, src, w, h, level.data());
            src += dxt_level_bytes(d, w, h);
            for (size_t i = 3; i < level.size() && opaque; i += 4) opaque = level[i] == 255;
        }
    gl_format = family == TVTX_ASTC ? TVTX_GL_ASTC_4x4 : opaque ? TVTX_GL_ETC2_RGB8 : TVTX_GL_ETC2_RGBA8;
    flags = opaque ? TVTX_OPAQUE : 0u;
    uint64_t total = 0;
    for (uint32_t l = 0; l < d.mips; ++l) {
        uint32_t w, h;
        level_dims(d, l, w, h);
        total += tvtx_level_bytes(gl_format, w, h);
    }
    payload.assign(static_cast<size_t>(total * d.faces), 0);
    uint8_t *dst = payload.data();
    for (uint32_t f = 0; f < d.faces; ++f)
        for (uint32_t l = 0; l < d.mips; ++l) {
            uint32_t w, h;
            level_dims(d, l, w, h);
            if (!encode_level(gl_format, levels[static_cast<size_t>(f) * d.mips + l].data(), w, h, opaque, dst)) return -1;
            dst += tvtx_level_bytes(gl_format, w, h);
        }
    return 0;
}

tvtx_crc_tables crc_tables;

uint64_t key_of(const uint8_t *data, size_t n)
{
    static std::once_flag once;
    std::call_once(once, [] { tvtx_crc_init(&crc_tables); });
    return tvtx_key(&crc_tables, data, n);
}

}
extern "C" int texconv_convert_dds(const uint8_t *dds, uint64_t size, int family, uint8_t **out, uint64_t *out_size,
    uint32_t *gl_format, uint32_t *flags)
{
    std::vector<uint8_t> payload;
    if (convert_dds(dds, size, family, payload, *gl_format, *flags)) return -1;
    *out = static_cast<uint8_t *>(malloc(payload.size() ? payload.size() : 1));
    if (!*out) return -1;
    memcpy(*out, payload.data(), payload.size());
    *out_size = payload.size();
    return 0;
}

extern "C" void texconv_free(void *pointer) { free(pointer); }

extern "C" int texconv_decode_dxt(const uint8_t *dds, uint64_t size, uint32_t face, uint32_t level, uint8_t *rgba)
{
    tvtx_dds d;
    if (!tvtx_dds_bytes(dds, size, &d) || face >= d.faces || level >= d.mips) return -1;
    const uint8_t *src = dds + 128;
    for (uint32_t f = 0; f <= face; ++f)
        for (uint32_t l = 0; l < d.mips; ++l) {
            uint32_t w, h;
            level_dims(d, l, w, h);
            if (f == face && l == level) {
                decode_dxt(d.fourcc, src, w, h, rgba);
                return 0;
            }
            src += dxt_level_bytes(d, w, h);
        }
    return -1;
}

extern "C" int texconv_decode_converted(uint32_t gl_format, const uint8_t *blocks, uint32_t w, uint32_t h, uint8_t *rgba)
{
    if (gl_format == TVTX_GL_ETC2_RGB8 || gl_format == TVTX_GL_ETC2_RGBA8) {
        uint32_t pw = (w + 3) & ~3u, ph = (h + 3) & ~3u;
        std::vector<uint32_t> decoded(static_cast<size_t>(pw) * ph);   /* RGBA words */
        std::vector<uint64_t> words(tvtx_level_bytes(gl_format, w, h) / 8);
        memcpy(words.data(), blocks, words.size() * 8);
        if (gl_format == TVTX_GL_ETC2_RGB8) DecodeRGB(words.data(), decoded.data(), static_cast<int32_t>(pw), static_cast<int32_t>(ph));
        else DecodeRGBA(words.data(), decoded.data(), static_cast<int32_t>(pw), static_cast<int32_t>(ph));
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                uint32_t v = decoded[static_cast<size_t>(y) * pw + x];
                uint8_t *p = rgba + (static_cast<size_t>(y) * w + x) * 4;
                p[0] = static_cast<uint8_t>(v);
                p[1] = static_cast<uint8_t>(v >> 8);
                p[2] = static_cast<uint8_t>(v >> 16);
                p[3] = gl_format == TVTX_GL_ETC2_RGB8 ? 255 : static_cast<uint8_t>(v >> 24);
            }
        return 0;
    }
    if (gl_format != TVTX_GL_ASTC_4x4) return -1;
    astcenc_context *context = astc.get(false);
    if (!context) return -1;
    astcenc_image image{};
    image.dim_x = w;
    image.dim_y = h;
    image.dim_z = 1;
    image.data_type = ASTCENC_TYPE_U8;
    void *slices[] = {rgba};
    image.data = slices;
    const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
    astcenc_error status = astcenc_decompress_image(context, blocks, tvtx_level_bytes(gl_format, w, h), &image, &swizzle, 0);
    astcenc_decompress_reset(context);
    return status == ASTCENC_SUCCESS ? 0 : -1;
}

extern "C" uint64_t texconv_key(const uint8_t *data, uint64_t size) { return key_of(data, static_cast<size_t>(size)); }
