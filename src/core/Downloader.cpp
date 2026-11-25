#include "core/Downloader.h"
#include "core/Paths.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

Downloader::Downloader(int concurrency, int fileThreads, long long chunkThresholdBytes) {
    xgdl::Config cfg;
    if (concurrency < 1) concurrency = 1;
    if (concurrency > 48) concurrency = 48;
    if (fileThreads < 1) fileThreads = 1;
    if (fileThreads > 128) fileThreads = 128;
    // 分块阈值夹在 128 KB ~ 8 MB
    long long th = chunkThresholdBytes;
    if (th < 128ll << 10) th = 128ll << 10;
    if (th > 8ll << 20) th = 8ll << 20;
    cfg.smallThreads = concurrency;
    // 大文件分块并发：就是设置里的"单文件下载线程"，1~128
    cfg.largeThreads = fileThreads;
    cfg.largeThreshold = (uint64_t)th;
    // 连接池要撑得住：分块线程再多，实际连接数也不能被 maxConnections 卡住
    cfg.maxConnections = (std::max)(48, fileThreads + concurrency);
    cfg.maxHostConnections = (std::max)(16, fileThreads);
    mgr_ = std::make_unique<xgdl::Manager>(cfg);
    mgr_->Start();
}

Downloader::~Downloader() {
    if (mgr_) {
        mgr_->CancelAll();
        mgr_->Shutdown();
    }
}

bool Downloader::Enqueue(const std::wstring& url, const std::wstring& path,
                         const std::wstring& sha1, long long expectBytes,
                         const std::vector<std::wstring>& mirrors) {
    if (!mgr_ || url.empty() || path.empty()) return false;
    std::wstring dir = fs::path(path).parent_path().wstring();
    EnsureDir(dir);
    xgdl::TaskOptions opt;
    opt.expectBytes = expectBytes > 0 ? (uint64_t)expectBytes : 0;
    if (!sha1.empty()) {
        opt.hash.alg = xgdl::HashAlg::Sha1;
        opt.hash.hex = sha1;
    }
    opt.resume = true;
    opt.mirrors = mirrors;
    return mgr_->Add(url, path, opt) != 0;
}

void Downloader::Tick() {
    if (!mgr_) return;
    prog_ = mgr_->Snapshot();
    xgdl::Event ev;
    while (mgr_->PollEvent(ev)) events_.push_back(ev);
}

std::vector<xgdl::Event> Downloader::TakeEvents() {
    std::vector<xgdl::Event> out;
    out.swap(events_);
    return out;
}

bool Downloader::Busy() const {
    return prog_.queued > 0 || prog_.active > 0;
}
