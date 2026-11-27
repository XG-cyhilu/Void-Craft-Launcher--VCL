#include "core/Downloader.h"
#include "core/Paths.h"
#include "core/Text.h"

#include <windows.h>
#include <xgcrypto/xgcrypto.h>
#include <zoe/zoe.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace {

struct Job {
    std::wstring path;
    std::wstring sha1;
    std::vector<std::wstring> urls;
    size_t nextUrl = 0;
    long long expectBytes = 0;

    std::atomic<long long> total{ 0 };
    std::atomic<long long> done{ 0 };
    std::atomic<bool> finished{ false };
    bool ok = false;
    std::wstring msg;

    std::mutex zMutex;
    std::shared_ptr<zoe::Zoe> z;
    std::atomic<bool> canceled{ false };
};

using JobPtr = std::shared_ptr<Job>;

void Configure(zoe::Zoe& z, int fileThreads) {
    z.setThreadNum(fileThreads);
    z.setNetworkConnectionTimeout(15000);
    z.setRetryTimesOfFetchFileInfo(2);
    z.setExpiredTimeOfTmpFile(7 * 24 * 3600);              // 临时文件留一周，断了能续
    z.setUncompletedSliceSavePolicy(zoe::UncompletedSliceSavePolicy::SaveExceptFailed);
    z.setVerifyCAEnabled(true, "");                        // Schannel 后端走系统证书库
    z.setVerifyHostEnabled(true);
}

bool Sha1Matches(const std::wstring& expect, const std::wstring& path) {
    std::string got = xgcrypto::HashFileHex(xgcrypto::Alg::Sha1, path);
    if (got.empty()) return false;
    std::wstring gotW = Utf8ToWide(got);
    std::wstring exp = expect;
    exp.erase(std::remove_if(exp.begin(), exp.end(), [](wchar_t c) { return c == L' ' || c == L'\n'; }),
              exp.end());
    std::transform(exp.begin(), exp.end(), exp.begin(), ::tolower);
    std::transform(gotW.begin(), gotW.end(), gotW.begin(), ::tolower);
    return gotW == exp;
}

void RunJob(JobPtr job, int fileThreads) {
    for (; job->nextUrl < job->urls.size(); ++job->nextUrl) {
        if (job->canceled.load()) {
            job->msg = L"已取消";
            break;
        }

        auto z = std::make_shared<zoe::Zoe>();
        Configure(*z, fileThreads);
        {
            std::lock_guard<std::mutex> lk(job->zMutex);
            job->z = z;
        }

        auto fut = z->start(
            job->urls[job->nextUrl], job->path,
            [](zoe::ZoeResult) {},
            [job](int64_t total, int64_t downloaded) {
                if (total > 0) job->total.store(total);
                job->done.store(downloaded > 0 ? downloaded : 0);
            },
            [](int64_t) {});

        zoe::ZoeResult r = fut.get();
        {
            std::lock_guard<std::mutex> lk(job->zMutex);
            job->z.reset();
        }
        if (r == zoe::ZoeResult::SUCCESSED) {
            job->ok = true;
            break;
        }
        if (r == zoe::ZoeResult::CANCELED) {
            job->msg = L"已取消";
            break;
        }
        job->msg = Utf8ToWide(zoe::Zoe::GetResultString(r));
    }

    if (job->ok && !job->sha1.empty()) {
        if (!Sha1Matches(job->sha1, job->path)) {
            job->ok = false;
            job->msg = L"SHA-1 校验失败";
            std::error_code ec;
            fs::remove(job->path, ec);
        }
    }
    job->finished.store(true);
}

} // namespace

struct Downloader::Impl {
    int concurrency = 4;
    int fileThreads = 8;

    std::deque<JobPtr> queued;
    std::vector<JobPtr> active;
    std::vector<DownloadEvent> events;

    long long lastDone = 0;
    long long speed = 0;
    unsigned long long lastTickMs = 0;

    void StartJob(const JobPtr& job) {
        active.push_back(job);
        std::thread([job, ft = fileThreads] { RunJob(job, ft); }).detach();
    }

    void Reap() {
        for (size_t i = 0; i < active.size();) {
            auto& job = active[i];
            if (!job->finished.load()) {
                ++i;
                continue;
            }
            DownloadEvent ev;
            ev.path = job->path;
            if (job->ok) {
                ev.kind = DownloadEvent::Completed;
            } else {
                ev.kind = DownloadEvent::Failed;
                ev.message = job->msg.empty() ? L"下载失败" : job->msg;
            }
            events.push_back(ev);
            active.erase(active.begin() + i);
        }
    }
};

Downloader::Downloader(int concurrency, int fileThreads, long long) : impl_(std::make_unique<Impl>()) {
    zoe::Zoe::GlobalInit();
    if (concurrency < 1) concurrency = 1;
    // Zoe 自己就会给单个文件开分片线程，任务级并发再大只会互相拖慢
    if (concurrency > 8) concurrency = 8;
    if (fileThreads < 1) fileThreads = 1;
    if (fileThreads > 100) fileThreads = 100;   // Zoe 上限
    impl_->concurrency = concurrency;
    impl_->fileThreads = fileThreads;
    impl_->lastTickMs = GetTickCount64();
}

Downloader::~Downloader() {
    for (auto& job : impl_->active) {
        job->canceled.store(true);
        std::lock_guard<std::mutex> lk(job->zMutex);
        if (job->z) job->z->stop();
    }
    // 让工作线程看到取消后自己收尾；这里不 join，避免关窗口时卡住。
    // 不做 GlobalUnInit：分离出去的工作线程可能还在用 curl，进程退出时由系统回收。
    impl_->active.clear();
}

bool Downloader::Enqueue(const std::wstring& url, const std::wstring& path,
                         const std::wstring& sha1, long long expectBytes,
                         const std::vector<std::wstring>& mirrors) {
    if (url.empty() || path.empty()) return false;
    std::wstring dir = fs::path(path).parent_path().wstring();
    EnsureDir(dir);

    auto job = std::make_shared<Job>();
    job->path = path;
    job->sha1 = sha1;
    job->expectBytes = expectBytes;
    if (expectBytes > 0) job->total.store(expectBytes);
    job->urls.push_back(url);
    for (const auto& m : mirrors) {
        if (!m.empty() && m != url) job->urls.push_back(m);
    }
    impl_->queued.push_back(job);
    return true;
}

void Downloader::Tick() {
    auto& s = *impl_;
    s.Reap();
    while (!s.queued.empty() && (int)s.active.size() < s.concurrency) {
        s.StartJob(s.queued.front());
        s.queued.pop_front();
    }

    long long total = 0, done = 0;
    for (auto& job : s.active) {
        total += job->total.load();
        done += job->done.load();
    }

    unsigned long long now = GetTickCount64();
    unsigned long long dt = now - s.lastTickMs;
    if (dt >= 250) {
        long long delta = done - s.lastDone;
        s.speed = dt > 0 ? (long long)(delta * 1000 / (long long)dt) : 0;
        s.lastDone = done;
        s.lastTickMs = now;
    }
}

DownloadProgress Downloader::Progress() const {
    DownloadProgress p;
    p.queued = (int)impl_->queued.size();
    p.active = (int)impl_->active.size();
    long long total = 0, done = 0;
    for (auto& job : impl_->active) {
        total += job->total.load();
        done += job->done.load();
    }
    p.bytesTotal = total;
    p.bytesDone = done;
    p.bytesPerSec = impl_->speed;
    return p;
}

std::vector<DownloadEvent> Downloader::TakeEvents() {
    std::vector<DownloadEvent> out;
    out.swap(impl_->events);
    return out;
}

bool Downloader::Busy() const {
    return !impl_->queued.empty() || !impl_->active.empty();
}

