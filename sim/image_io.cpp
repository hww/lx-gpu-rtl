#include "image_io.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// =========================================================
// Палитры
// =========================================================

// EGA/VGA 16 цветов (0x00RRGGBB)
const uint32_t vga16_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

// Web-safe 216 + 40 серых. Инициализация один раз.
static uint32_t g_websafe216[256];
static bool     g_websafe216_done = false;

static void init_websafe216() {
    if (g_websafe216_done) return;
    int idx = 0;
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++) {
                uint32_t R = (uint32_t)(r * 51);
                uint32_t G = (uint32_t)(g * 51);
                uint32_t B = (uint32_t)(b * 51);
                g_websafe216[idx++] = (R << 16) | (G << 8) | B;
            }
    // 40 градаций серого (0..255)
    for (int i = 0; i < 40; i++) {
        uint32_t v = (uint32_t)((i * 255 + 39) / 40);
        g_websafe216[idx++] = (v << 16) | (v << 8) | v;
    }
    g_websafe216_done = true;
}

const uint32_t* get_websafe216_palette() {
    init_websafe216();
    return g_websafe216;
}

// =========================================================
// Утилиты
// =========================================================

int bits_per_pixel(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::BPP1:  return 1;
        case PixelFormat::BPP4:  return 4;
        case PixelFormat::BPP8:  return 8;
        case PixelFormat::BPP16: return 16;
    }
    return 0;
}

bool parse_format(const std::string& s, PixelFormat& fmt) {
    if (s == "1bpp")  { fmt = PixelFormat::BPP1;  return true; }
    if (s == "4bpp")  { fmt = PixelFormat::BPP4;  return true; }
    if (s == "8bpp")  { fmt = PixelFormat::BPP8;  return true; }
    if (s == "16bpp") { fmt = PixelFormat::BPP16; return true; }
    return false;
}

const char* format_name(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::BPP1:  return "1bpp";
        case PixelFormat::BPP4:  return "4bpp";
        case PixelFormat::BPP8:  return "8bpp";
        case PixelFormat::BPP16: return "16bpp";
    }
    return "?";
}

bool check_resolution(int w, int h, PixelFormat fmt, std::string& err) {
    int bpp = bits_per_pixel(fmt);
    if (bpp == 0) { err = "unknown format"; return false; }
    uint64_t need_bits  = (uint64_t)w * h * bpp;
    uint64_t need_bytes = (need_bits + 7) / 8;
    if (need_bytes > 65536) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "resolution %dx%d @ %dbpp needs %llu bytes, max 65536",
                 w, h, bpp, (unsigned long long)need_bytes);
        err = buf;
        return false;
    }
    return true;
}

// =========================================================
// I/O
// =========================================================

bool read_image(const std::string& path, Image& out) {
    int w = 0, h = 0, n = 0;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) {
        fprintf(stderr, "read_image: cannot load '%s': %s\n",
                path.c_str(), stbi_failure_reason());
        return false;
    }
    out.width  = w;
    out.height = h;
    out.rgba.assign(data, data + (size_t)w * h * 4);
    stbi_image_free(data);
    return true;
}

bool write_image(const std::string& path, const Image& img) {
    if (img.rgba.size() != (size_t)img.width * img.height * 4) {
        fprintf(stderr, "write_image: bad rgba size\n");
        return false;
    }
    int ok = stbi_write_png(path.c_str(), img.width, img.height,
                            4, img.rgba.data(), img.width * 4);
    if (!ok) {
        fprintf(stderr, "write_image: cannot write '%s'\n", path.c_str());
        return false;
    }
    return true;
}

// =========================================================
// Квантование / де-квантование
// =========================================================

static inline uint32_t dist2(uint32_t a, uint32_t b) {
    int dr = (int)((a >> 16) & 0xFF) - (int)((b >> 16) & 0xFF);
    int dg = (int)((a >> 8)  & 0xFF) - (int)((b >> 8)  & 0xFF);
    int db = (int)( a        & 0xFF) - (int)( b        & 0xFF);
    return (uint32_t)(dr*dr + dg*dg + db*db);
}

std::vector<uint8_t> quantize(const Image& img, PixelFormat fmt,
                              const uint32_t* palette, int palette_size) {
    size_t n_pixels = (size_t)img.width * img.height;

    if (fmt == PixelFormat::BPP16) {
        std::vector<uint8_t> out(n_pixels * 2);
        for (size_t i = 0; i < n_pixels; i++) {
            uint8_t R = img.rgba[i*4+0];
            uint8_t G = img.rgba[i*4+1];
            uint8_t B = img.rgba[i*4+2];
            uint16_t v = (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3));
            out[i*2+0] = (uint8_t)(v & 0xFF);
            out[i*2+1] = (uint8_t)((v >> 8) & 0xFF);
        }
        return out;
    }

    // 1/4/8 bpp: 1 байт на индекс
    std::vector<uint8_t> out(n_pixels);
    for (size_t i = 0; i < n_pixels; i++) {
        uint32_t rgb = ((uint32_t)img.rgba[i*4+0] << 16) |
                       ((uint32_t)img.rgba[i*4+1] << 8)  |
                        (uint32_t)img.rgba[i*4+2];
        uint32_t best_d = 0xFFFFFFFFu;
        int best_i = 0;
        for (int j = 0; j < palette_size; j++) {
            uint32_t d = dist2(rgb, palette[j]);
            if (d < best_d) { best_d = d; best_i = j; }
        }
        out[i] = (uint8_t)best_i;
    }
    return out;
}

Image dequantize(const std::vector<uint8_t>& indices, int w, int h,
                 PixelFormat fmt, const uint32_t* palette) {
    Image out;
    out.width = w;
    out.height = h;
    out.rgba.resize((size_t)w * h * 4);
    size_t n_pixels = (size_t)w * h;

    if (fmt == PixelFormat::BPP16) {
        for (size_t i = 0; i < n_pixels; i++) {
            uint16_t v = (uint16_t)(indices[i*2] | (indices[i*2+1] << 8));
            uint8_t R = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
            uint8_t G = (uint8_t)(((v >> 5)  & 0x3F) * 255 / 63);
            uint8_t B = (uint8_t)(( v        & 0x1F) * 255 / 31);
            out.rgba[i*4+0] = R;
            out.rgba[i*4+1] = G;
            out.rgba[i*4+2] = B;
            out.rgba[i*4+3] = 255;
        }
        return out;
    }

    for (size_t i = 0; i < n_pixels; i++) {
        uint32_t c = palette[indices[i]];
        out.rgba[i*4+0] = (uint8_t)((c >> 16) & 0xFF);
        out.rgba[i*4+1] = (uint8_t)((c >> 8)  & 0xFF);
        out.rgba[i*4+2] = (uint8_t)( c        & 0xFF);
        out.rgba[i*4+3] = 255;
    }
    return out;
}

// =========================================================
// Упаковка / распаковка
// =========================================================

std::vector<uint16_t> pack_pixels(const std::vector<uint8_t>& indices,
                                  int w, int h, PixelFormat fmt) {
    size_t n_pixels = (size_t)w * h;
    std::vector<uint16_t> words;

    if (fmt == PixelFormat::BPP16) {
        words.resize(n_pixels);
        for (size_t i = 0; i < n_pixels; i++) {
            words[i] = (uint16_t)(indices[i*2] | (indices[i*2+1] << 8));
        }
        return words;
    }

    if (fmt == PixelFormat::BPP8) {
        size_t n_words = (n_pixels + 1) / 2;
        words.assign(n_words, 0);
        for (size_t i = 0; i < n_pixels; i++) {
            size_t wi = i / 2;
            int pos = (int)(i & 1);
            // пиксель 0 = младший байт (как видео Aleste и ch_blender)
            if (pos == 0) words[wi] |= (uint16_t)indices[i];
            else          words[wi] |= (uint16_t)((uint16_t)indices[i] << 8);
        }
        return words;
    }

    if (fmt == PixelFormat::BPP4) {
        size_t n_words = (n_pixels + 3) / 4;
        words.assign(n_words, 0);
        for (size_t i = 0; i < n_pixels; i++) {
            size_t wi = i / 4;
            int pos = (int)(i & 3);
            int shift = ((3 - pos) * 4) ^ 8;   // 7..4, 3..0, 15..12, 11..8
            words[wi] |= (uint16_t)(((uint16_t)(indices[i] & 0xF)) << shift);
        }
        return words;
    }

    if (fmt == PixelFormat::BPP1) {
        size_t n_words = (n_pixels + 15) / 16;
        words.assign(n_words, 0);
        for (size_t i = 0; i < n_pixels; i++) {
            size_t wi = i / 16;
            int pos = (int)(i & 15);
            int shift = (15 - pos) ^ 8;        // бит 7 .. 0, затем 15 .. 8
            words[wi] |= (uint16_t)(((uint16_t)(indices[i] & 1)) << shift);
        }
        return words;
    }

    return words;
}

std::vector<uint8_t> unpack_pixels(const std::vector<uint16_t>& words,
                                   int w, int h, PixelFormat fmt) {
    size_t n_pixels = (size_t)w * h;

    if (fmt == PixelFormat::BPP16) {
        std::vector<uint8_t> out(n_pixels * 2);
        for (size_t i = 0; i < n_pixels; i++) {
            uint16_t v = words[i];
            out[i*2+0] = (uint8_t)(v & 0xFF);
            out[i*2+1] = (uint8_t)((v >> 8) & 0xFF);
        }
        return out;
    }

    std::vector<uint8_t> out(n_pixels, 0);

    if (fmt == PixelFormat::BPP8) {
        for (size_t i = 0; i < n_pixels; i++) {
            size_t wi = i / 2;
            int pos = (int)(i & 1);
            out[i] = (pos == 0) ? (uint8_t)( words[wi]       & 0xFF)
                                : (uint8_t)((words[wi] >> 8) & 0xFF);
        }
        return out;
    }

    if (fmt == PixelFormat::BPP4) {
        for (size_t i = 0; i < n_pixels; i++) {
            size_t wi = i / 4;
            int pos = (int)(i & 3);
            int shift = ((3 - pos) * 4) ^ 8;   // 7..4, 3..0, 15..12, 11..8
            out[i] = (uint8_t)((words[wi] >> shift) & 0xF);
        }
        return out;
    }

    if (fmt == PixelFormat::BPP1) {
        for (size_t i = 0; i < n_pixels; i++) {
            size_t wi = i / 16;
            int pos = (int)(i & 15);
            int shift = (15 - pos) ^ 8;        // бит 7 .. 0, затем 15 .. 8
            out[i] = (uint8_t)((words[wi] >> shift) & 0x1);
        }
        return out;
    }

    return out;
}