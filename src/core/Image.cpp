#include "core/Image.h"

#include <windows.h>
#include <wincodec.h>
#include <webp/decode.h>

#include <cstring>
#include <memory>

namespace {

struct ComReleaser {
    void operator()(IUnknown* p) const { if (p) p->Release(); }
};
template <class T>
using ComPtr = std::unique_ptr<T, ComReleaser>;

// WIC 兜底：PNG / JPEG / GIF / BMP。WebP 在这台机器上不一定有系统扩展，
// 所以 webp 一定走 libwebp，不能指望 WIC。
bool DecodeWithWic(const std::vector<unsigned char>& bytes, RgbaImage& out, std::string* err) {
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory));
    }
    ComPtr<IWICImagingFactory> fac(factory);
    if (!fac) {
        if (err) *err = "无法创建 WIC 工厂";
        return false;
    }

    IWICStream* streamRaw = nullptr;
    if (FAILED(fac->CreateStream(&streamRaw)) || !streamRaw) {
        if (err) *err = "WIC stream 创建失败";
        return false;
    }
    ComPtr<IWICStream> stream(streamRaw);
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),
                                            static_cast<DWORD>(bytes.size())))) {
        if (err) *err = "WIC 内存流初始化失败";
        return false;
    }

    IWICBitmapDecoder* decRaw = nullptr;
    hr = fac->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, &decRaw);
    ComPtr<IWICBitmapDecoder> dec(decRaw);
    if (!dec) {
        if (err) *err = "未识别的图片格式（WIC）";
        return false;
    }

    IWICBitmapFrameDecode* frameRaw = nullptr;
    if (FAILED(dec->GetFrame(0, &frameRaw)) || !frameRaw) {
        if (err) *err = "图片没有可解码的帧";
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame(frameRaw);

    IWICFormatConverter* convRaw = nullptr;
    if (FAILED(fac->CreateFormatConverter(&convRaw)) || !convRaw) {
        if (err) *err = "WIC 格式转换器创建失败";
        return false;
    }
    ComPtr<IWICFormatConverter> conv(convRaw);
    if (FAILED(conv->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom))) {
        if (err) *err = "WIC 转 RGBA 失败";
        return false;
    }

    UINT w = 0, h = 0;
    if (FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0) {
        if (err) *err = "图片尺寸无效";
        return false;
    }
    out.width = static_cast<int>(w);
    out.height = static_cast<int>(h);
    out.pixels.assign(static_cast<size_t>(w) * h * 4, 0);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.pixels.size()),
                                out.pixels.data()))) {
        out.Reset();
        if (err) *err = "WIC 像素拷贝失败";
        return false;
    }
    return true;
}

} // namespace

bool LooksLikeWebp(const std::vector<unsigned char>& bytes) {
    // RIFF 头 + 偏移 8 处的 "WEBP"
    return bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
           std::memcmp(bytes.data() + 8, "WEBP", 4) == 0;
}

bool DecodeImageToRgba(const std::vector<unsigned char>& bytes, RgbaImage& out, std::string* err) {
    out.Reset();
    if (bytes.empty()) {
        if (err) *err = "图片数据为空";
        return false;
    }

    if (LooksLikeWebp(bytes)) {
        // 静态 WebP 直接解；带动画的那类第一帧也能出来
        int w = 0, h = 0;
        if (!WebPGetInfo(bytes.data(), bytes.size(), &w, &h) || w <= 0 || h <= 0) {
            if (err) *err = "WebP 头解析失败";
            return false;
        }
        std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
        if (!WebPDecodeRGBAInto(bytes.data(), bytes.size(), rgba.data(),
                                static_cast<size_t>(w) * h * 4, w * 4)) {
            if (err) *err = "WebP 解码失败";
            return false;
        }
        out.width = w;
        out.height = h;
        out.pixels = std::move(rgba);
        return true;
    }

    return DecodeWithWic(bytes, out, err);
}
