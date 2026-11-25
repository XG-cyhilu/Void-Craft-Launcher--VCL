#include "core/IconStore.h"

#include "core/Paths.h"

#include "vcui/Surface.h"

#include <fstream>

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

namespace vcl {

IconStore::IconStore(vcui::Surface* surface) : surface_(surface) {
    cacheDir_ = JoinPath(AppCacheDir(), L"icons");
    EnsureDir(cacheDir_);
    worker_ = std::thread([this] { Worker(); });
}

IconStore::~IconStore() {
    {
        std::lock_guard<std::mutex> lk(jm_);
        quit_ = true;
    }
    jcv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

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

void IconStore::SetWake(std::function<void()> fn) {
    wake_ = std::move(fn);
    http_.onWake = wake_;
}

void IconStore::Enqueue(Job job) {
    {
        std::lock_guard<std::mutex> lk(jm_);
        jobs_.push(std::move(job));
    }
    jcv_.notify_one();
}

void IconStore::Worker() {
    // WIC 需要在调用线程上初始化 COM；这里用 MTA，跟 UI 的 STA 互不干扰
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(jm_);
            jcv_.wait(lk, [this] { return quit_ || !jobs_.empty(); });
            if (quit_ && jobs_.empty()) return;
            job = std::move(jobs_.front());
            jobs_.pop();
        }

        std::vector<unsigned char> bytes = std::move(job.bytes);
        const bool localFile = !job.localPath.empty();
        if (localFile) {
            std::ifstream in(job.localPath, std::ios::binary);
            if (in) bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        } else if (job.fromDisk) {
            std::wstring path;
            CachePath(job.url, path);
            std::ifstream in(path, std::ios::binary);
            if (in) {
                bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }
        }

        Result r;
        r.url = job.url;
        r.fromDisk = job.fromDisk;
        r.fromLocal = localFile;
        if (bytes.empty()) {
            r.failed = true;
        } else {
            auto img = std::make_shared<RgbaImage>();
            if (!DecodeImageToRgba(bytes, *img)) {
                r.failed = true;
            } else {
                r.img = std::move(img);
                // 来自网络的才回写磁盘，省一次读-解-写；本地资源本来就是文件，不用再缓存一份
                if (!job.fromDisk && !localFile) {
                    std::wstring path;
                    CachePath(job.url, path);
                    std::ofstream out(path, std::ios::binary | std::ios::trunc);
                    if (out) out.write(reinterpret_cast<const char*>(bytes.data()),
                                       static_cast<std::streamsize>(bytes.size()));
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk(rm_);
            results_.push_back(std::move(r));
        }
        if (wake_) wake_();
    }
    CoUninitialize();
}

ID2D1Bitmap* IconStore::Acquire(const std::wstring& url) {
    if (url.empty()) return nullptr;
    Entry& e = At(url);
    if (EnsureBitmap(e)) return e.bmp.Get();
    if (e.failed) return nullptr;

    if (!e.requested) {
        e.requested = true;
        if (url.rfind(L"file:", 0) == 0) {
            // resource 里自带的图标：后台直接读本地文件，不走网络也不写缓存
            Job job;
            job.url = url;
            job.localPath = url.substr(5);
            Enqueue(std::move(job));
            return nullptr;
        }
        // 磁盘命中就走"读盘 + 解码"的后台任务，否则才发网络请求
        std::wstring path;
        CachePath(url, path);
        if (FileExists(path)) {
            Job job;
            job.url = url;
            job.fromDisk = true;
            Enqueue(std::move(job));
        } else {
            http_.Get(url, [this, url](bool ok, const std::string& body, const std::string&) {
                IconStore::Entry& en = At(url);
                if (!ok || body.empty()) {
                    en.failed = true;
                    return;
                }
                Job job;
                job.url = url;
                job.bytes.assign(body.begin(), body.end());
                Enqueue(std::move(job));
            }, {}, 20000);
        }
    }
    return nullptr;
}

void IconStore::DrainResults() {
    std::vector<Result> ready;
    {
        std::lock_guard<std::mutex> lk(rm_);
        ready.swap(results_);
    }
    if (ready.empty()) return;
    for (auto& r : ready) {
        Entry& e = At(r.url);
        if (r.failed || !r.img) {
            if (r.fromLocal) {
                // 本地资源读不出来就是资源缺失，不要再重试
                e.failed = true;
            } else if (r.fromDisk) {
                // 缓存文件坏了：删掉，下次重新走网络
                std::wstring path;
                CachePath(r.url, path);
                DeleteFileW(path.c_str());
                e.requested = false;
                e.failed = false;
            } else {
                e.failed = true;
            }
            continue;
        }
        e.img = std::move(r.img);
    }
    if (surface_) surface_->Invalidate();
}

void IconStore::Pump() {
    http_.Pump();
    DrainResults();
}

bool IconStore::Busy() {
    if (http_.Busy()) return true;
    std::lock_guard<std::mutex> lk(jm_);
    return !jobs_.empty();
}

} // namespace vcl


