#include "image_io.h"
#include <cstdio>
#include <cstring>
#include <string>

static void usage(const char* prog) {
    fprintf(stderr,
        "usage: %s <in.png> <out.png> <fmt>\n"
        "  fmt: 1bpp | 4bpp | 8bpp | 16bpp\n"
        "  Тест: PNG -> pack -> mem[] -> unpack -> PNG (без GPU)\n",
        prog);
}

int main(int argc, char** argv) {
    if (argc != 4) { usage(argv[0]); return 1; }

    const char* in_path  = argv[1];
    const char* out_path = argv[2];
    const char* fmt_str  = argv[3];

    PixelFormat fmt;
    if (!parse_format(fmt_str, fmt)) {
        fprintf(stderr, "unknown format: %s\n", fmt_str);
        return 1;
    }

    Image img;
    if (!read_image(in_path, img)) return 1;
    printf("read  %s: %dx%d\n", in_path, img.width, img.height);

    std::string err;
    if (!check_resolution(img.width, img.height, fmt, err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    // Выбор палитры
    const uint32_t* palette = nullptr;
    int palette_size = 0;
    if (fmt == PixelFormat::BPP4) {
        palette = vga16_palette; palette_size = 16;
    } else if (fmt == PixelFormat::BPP8) {
        palette = get_websafe216_palette(); palette_size = 256;
    } else if (fmt == PixelFormat::BPP1) {
        palette = vga16_palette; palette_size = 2;  // только 2 цвета
    }

    // 1) RGBA -> индексы
    std::vector<uint8_t> indices = quantize(img, fmt, palette, palette_size);

    // 2) индексы -> слова (эмуляция того, что лежит в mem[])
    std::vector<uint16_t> words = pack_pixels(indices, img.width, img.height, fmt);
    printf("packed %zu words (%zu bytes)\n", words.size(), words.size() * 2);

    // 3) слова -> индексы (эмуляция чтения из mem[] через pixel_packer)
    std::vector<uint8_t> indices2 = unpack_pixels(words, img.width, img.height, fmt);

    // 4) индексы -> RGBA -> PNG
    Image out = dequantize(indices2, img.width, img.height, fmt, palette);
    if (!write_image(out_path, out)) return 1;
    printf("wrote %s: %dx%d (%s)\n", out_path, out.width, out.height, format_name(fmt));

    // Проверка идемпотентности
    int diff = 0;
    for (size_t i = 0; i < indices.size(); i++) {
        if (indices[i] != indices2[i]) diff++;
    }
    if (diff != 0) {
        fprintf(stderr, "WARNING: %d indices differ after roundtrip!\n", diff);
        return 1;
    }
    printf("roundtrip OK: %zu pixels, 0 differences\n", indices.size());
    return 0;
}