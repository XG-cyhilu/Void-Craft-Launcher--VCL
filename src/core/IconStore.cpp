#include "core/IconStore.h"

#include "core/Paths.h"

#include "vcui/Surface.h"

#include <fstream>

namespace vcl {

namespace {

// FNV-1a 64：URL → 稳定文件名，避免把 URL 里的斜杠、查询串带进路径
std::wstring FnvName(const std::wstring& s) {
    unsigned long long h = 1469598103934665603ULL;
    for (wchar_t c : s) {
        h ^= static_cast<unsigned long long>(static_cast<unsigned short>(c));
        h *= 1099511628211ULL;
    }
    wchar_t buf[32];
    swprintf_s(buf, L"%016llx.bin", h);
    return buf;
}

} // namespace

IconStore::IconStore(vcui::Surface* surface) : surface_(surface) {
    cacheDir_ = JoinPath(AppCacheDir(), L"icons");
    EnsureDir(cacheDir_);
}

IconStore::~IconStore() = default;

IconStore::Entry& IconStore::At(const std::wstring& url) {
    auto it = items_.find(url);
    if (it != items_.end()) return *it->second;
    auto e = std::make_unique<Entry>();
    Entry& ref = *e;
    items_.emplace(url, std::move(e));
    return ref;
}

void IconStore::CachePath(const std::wstring& url, std::wstring& out) const {
    out = JoinPath(cacheDir_, FnvName(url));
}

void IconStore::LoadFromDisk(Entry& e, const std::wstring& url) {
    std::wstring path;
    CachePath(url, path);
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (bytes.empty()) return;
    auto img = std::make_shared<RgbaImage>();
    if (DecodeImageToRgba(bytes, *img)) e.img = std::move(img);
}

bool IconStore::EnsureBitmap(Entry& e) {
    if (!e.img || !e.img->Valid()) return false;
    ID2D1RenderTarget* rt = surface_ ? surface_->G().rt.Get() : nullptr;
    if (!rt) return false;

    // 位图绑在渲染目标上；目标换了（设备丢失 / 尺寸重建）就得重传
    if (e.bmp && e.owner == rt) return true;
    e.bmp.Reset();
    e.owner = rt;

    const int w = e.img->width, h = e.img->height;
    std::vector<unsigned char> pm(static_cast<size_t>(w) * h * 4);
    const unsigned char* src = e.img->pixels.data();
    for (size_t i = 0, n = pm.size(); i < n; i += 4) {
        const unsigned a = src[i + 3];
        pm[i + 0] = static_cast<unsigned char>((src[i + 0] * a + 127) / 255);
        pm[i + 1] = static_cast<unsigned char>((src[i + 1] * a + 127) / 255);
        pm[i + 2] = static_cast<unsigned char>((src[i + 2] * a + 127) / 255);
        pm[i + 3] = static_cast<unsigned char>(a);
    }

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.f, 96.f);
    ID2D1Bitmap* bmp = nullptr;
    rt->CreateBitmap(D2D1::SizeU(static_cast<UINT>(w), static_cast<UINT>(h)),
                     pm.data(), static_cast<UINT>(w * 4), props, &bmp);
    e.bmp.Attach(bmp);
    return e.bmp != nullptr;
}

ID2D1Bitmap* IconStore::Acquire(const std::wstring& url) {
    if (url.empty()) return nullptr;
    Entry& e = At(url);
    if (EnsureBitmap(e)) return e.bmp.Get();
    if (e.failed) return nullptr;

    if (!e.requested) {
        e.requested = true;
        LoadFromDisk(e, url);
        if (EnsureBitmap(e)) return e.bmp.Get();

        // 磁盘没有 → 网络抓；回来后解码 + 落盘
        http_.Get(url, [this, url](bool ok, const std::string& body, const std::string&) {
            Entry& en = At(url);
            if (!ok || body.empty()) {
                en.failed = true;
                return;
            }
            std::vector<unsigned char> bytes(body.begin(), body.end());
            auto img = std::make_shared<RgbaImage>();
            if (!DecodeImageToRgba(bytes, *img)) {
                en.failed = true;
                return;
            }
            en.img = std::move(img);

            std::wstring path;
            CachePath(url, path);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (out) out.write(body.data(), static_cast<std::streamsize>(body.size()));

            if (surface_) surface_->Invalidate();
        }, {}, 20000);
    }
    return nullptr;
}

void IconStore::Pump() {
    http_.Pump();
}

bool IconStore::Busy() const { return http_.Busy(); }

} // namespace vcl

