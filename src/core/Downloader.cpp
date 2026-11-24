#include "core/Downloader.h"
#include "core/Paths.h"

#include <filesystem>

namespace fs = std::filesystem;

Downloader::Downloader(int concurrency) {
    xgdl::Config cfg;
    if (concurrency < 1) concurrency = 1;
    if (concurrency > 48) concurrency = 48;
    cfg.smallThreads = concurrency;
    cfg.largeThreads = (concurrency / 4) < 2 ? 2 : (concurrency / 4);
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
