#pragma once
#include <memory>
#include <string>
#include <vector>

// Zoe（libcurl 后端）的薄封装：自己维护任务队列，每个任务一个 Zoe 实例。
// 为什么不用 XGDL：XGDL 是自研的 CURLM 调度器，维护成本高；Zoe 成熟、限速/续传/分片齐全。
// Tick() 必须在 UI 线程里周期调用。
// concurrency = 同时进行的任务数；fileThreads = 单文件分片线程（1~100）；
// sha1 由封装自己算（Zoe 只支持 MD5/CRC32/SHA256）。
struct DownloadEvent {
    enum Kind { Completed, Failed };
    Kind kind = Completed;
    std::wstring path;
    std::wstring message;
};

struct DownloadProgress {
    int queued = 0;
    int active = 0;
    long long bytesTotal = 0;
    long long bytesDone = 0;
    long long bytesPerSec = 0;
};

class Downloader {
public:
    explicit Downloader(int concurrency = 16, int fileThreads = 8,
                        long long chunkThresholdBytes = 1 << 20);
    ~Downloader();

    // 目标路径所在目录会自动创建；sha1 为空表示不校验；mirrors 是主 URL 失败后的备用地址
    bool Enqueue(const std::wstring& url, const std::wstring& path,
                 const std::wstring& sha1 = L"", long long expectBytes = 0,
                 const std::vector<std::wstring>& mirrors = {});

    void Tick();

    DownloadProgress Progress() const;
    std::vector<DownloadEvent> TakeEvents();

    bool Busy() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

