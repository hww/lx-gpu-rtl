#pragma once
#include <cstdint>
#include <vector>
#include <string>

struct Image {
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // w*h*4, порядок R,G,B,A
};

enum class PixelFormat { BPP1, BPP4, BPP8, BPP16 };

// --- I/O ---
bool read_image(const std::string& path, Image& out);
bool write_image(const std::string& path, const Image& img);

// --- Палитры (указатели, гарантированно валидные после первого вызова) ---
// vga16: 16 цветов EGA/VGA, доступна всегда (статическая таблица).
extern const uint32_t vga16_palette[16];

// websafe216: 216 web-safe + 40 серых, инициализируется при первом обращении.
const uint32_t* get_websafe216_palette();   // вернёт указатель на 256 элементов

// --- Конвертация ---
std::vector<uint8_t>  quantize(const Image& img, PixelFormat fmt,
                               const uint32_t* palette, int palette_size);
Image                 dequantize(const std::vector<uint8_t>& indices, int w, int h,
                                 PixelFormat fmt, const uint32_t* palette);
std::vector<uint16_t> pack_pixels(const std::vector<uint8_t>& indices, int w, int h,
                                  PixelFormat fmt);
std::vector<uint8_t>  unpack_pixels(const std::vector<uint16_t>& words, int w, int h,
                                    PixelFormat fmt);

// --- Утилиты ---
int bits_per_pixel(PixelFormat fmt);
bool parse_format(const std::string& s, PixelFormat& fmt);
const char* format_name(PixelFormat fmt);
bool check_resolution(int w, int h, PixelFormat fmt, std::string& err);