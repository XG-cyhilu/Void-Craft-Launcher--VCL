#pragma once
#include <string>
#include <vector>

// 统一的位图解码出口：WebP 走 libwebp，其余（PNG / JPEG / GIF / BMP）走 WIC。
// 输出永远是 8 位直通 alpha 的 RGBA，交给 D2D 前再预乘，避免两套像素格式在界面层分叉。

struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;   // RGBA8，stride = width * 4

    bool Valid() const { return width > 0 && height > 0 && !pixels.empty(); }
    void Reset() { width = height = 0; pixels.clear(); }
};

bool DecodeImageToRgba(const std::vector<unsigned char>& bytes, RgbaImage& out,
                       std::string* err = nullptr);

// 是否像 WebP（RIFF....WEBP）。给缓存文件、调试输出用。
bool LooksLikeWebp(const std::vector<unsigned char>& bytes);
