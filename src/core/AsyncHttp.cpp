#include "core/AsyncHttp.h"
#include "core/Paths.h"
#include "core/Text.h"

#include "xgnet/xgnet.h"

#include <fstream>

namespace {

// FNV-1a 64：URL → 稳定文件名（和 IconStore 里的命名规则一致）
std::wstring CacheName(const std::wstring& url) {
    unsigned long long h = 1469598103934665603ULL;
    for (wchar_t c : url) {
        h ^= static_cast<unsigned long long>(static_cast<unsigned short>(c));
        h *= 1099511628211ULL;
    }
    wchar_t buf[32];
    swprintf_s(buf, L"%016llx.json", h);
    return buf;
}

} // namespace

AsyncHttp::AsyncHttp() {
    // 4 个取数线程：图标与清单不再一条队列串行，列表图能一波刷出来
    for (int i = 0; i < 4; ++i) workers_.emplace_back([this] { Worker(); });
}

AsyncHttp::~AsyncHttp() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) if (w.joinable()) w.join();
}

void AsyncHttp::Get(const std::wstring& url, Callback cb,
                    const std::vector<std::pair<std::wstring, std::wstring>>& headers,
                    int timeoutMs) {
    {
        std::lock_guard<std::mutex> lk(m_);
        jobs_.push(Job{ url, L"GET", {}, headers, timeoutMs, std::move(cb) });
        pending_++;
    }
    cv_.notify_one();
    if (onRequest) onRequest(url);
}

void AsyncHttp::Post(const std::wstring& url, const std::string& body, Callback cb,
                     const std::vector<std::pair<std::wstring, std::wstring>>& headers,
                     int timeoutMs) {
    {
        std::lock_guard<std::mutex> lk(m_);
        jobs_.push(Job{ url, L"POST", body, headers, timeoutMs, std::move(cb) });
        pending_++;
    }
    cv_.notify_one();
    if (onRequest) onRequest(url);
}

bool AsyncHttp::Busy() const { return pending_.load() > 0; }

void AsyncHttp::SetCacheDir(const std::wstring& dir) {
    cacheDir_ = dir;
    if (!dir.empty()) EnsureDir(dir);
}

void AsyncHttp::ClearCache() {
    if (cacheDir_.empty()) return;
    ClearTree(cacheDir_);
}

bool AsyncHttp::Pump() {
    std::vector<Done> ready;
    {
        std::lock_guard<std::mutex> lk(dm_);
        ready.swap(done_);
    }
    for (auto& d : ready) {
        if (d.cb) d.cb(d.ok, d.body, d.err);
    }
    return !ready.empty();
}

void AsyncHttp::Worker() {
    xgnet::Client client;
    while (true) {
        Job job;
        std::wstring cacheDir;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this] { return quit_ || !jobs_.empty(); });
            if (quit_ && jobs_.empty()) return;
            job = jobs_.front();
            jobs_.pop();
            cacheDir = cacheDir_;
        }

        // 会话缓存命中：直接回本地 JSON，不再走网络（省一次往返，也让"换页/刷新前"的结果可复用）
        // POST 不缓存：鉴权请求带令牌，缓存会把一次性的 code / token 回放出去
        std::wstring cachePath;
        bool useCache = !cacheDir.empty() && job.method == L"GET";
        if (useCache) cachePath = JoinPath(cacheDir, CacheName(job.url));

        Done d;
        d.cb = std::move(job.cb);

        std::string cached;
        if (useCache && FileExists(cachePath)) {
            std::ifstream in(cachePath, std::ios::binary);
            if (in) cached.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        if (!cached.empty()) {
            d.ok = true;
            d.body = std::move(cached);
        } else {
            xgnet::Request req;
            req.url = WideToUtf8(job.url);
            req.method = WideToUtf8(job.method);
            req.body = job.body;
            req.userAgent = "XGstudio-VCL/0.1 (+https://github.com/XG-cyhilu/Void-Craft-Launcher--VCL)";
            req.headers.emplace_back("Accept", "application/json");
            for (const auto& h : job.headers)
                req.headers.emplace_back(WideToUtf8(h.first), WideToUtf8(h.second));
            req.timeoutMs = (uint32_t)(job.timeoutMs > 1000 ? job.timeoutMs : 1000);

            xgnet::Response res;
            bool ok = client.Perform(req, res) && res.IsSuccess();
            d.ok = ok;
            d.body = res.body;
            if (!ok) {
                if (!res.error.empty()) d.err = res.error;
                else d.err = "HTTP " + std::to_string(res.status);
            } else if (useCache && !d.body.empty()) {
                std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
                if (out) out.write(d.body.data(), (std::streamsize)d.body.size());
            }
        }

        {
            std::lock_guard<std::mutex> lk(dm_);
            done_.push_back(std::move(d));
        }
        pending_--;
        if (onWake) onWake();
    }
}


