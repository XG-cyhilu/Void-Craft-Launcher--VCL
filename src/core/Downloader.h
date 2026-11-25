#pragma once
#include <string>
#include <vector>
#include <memory>

#include "xgdl/xgdl.h"

// XGDL 的薄封装：只做"入队 + 进度 + 事件"，线程与重试都在库里。
// Tick() 必须在 UI 线程里周期调用。
// concurrency = 小文件并发；fileThreads = 单文件分块线程（1~128，文件够大才用满）；
// chunkThresholdBytes = 超过该大小才分块（128 KB ~ 8 MB）。
class Downloader {
public:
    explicit Downloader(int concurrency = 16, int fileThreads = 8,
                        long long chunkThresholdBytes = 1 << 20);
    ~Downloader();

    // 目标路径所在目录会自动创建；sha1 为空表示不校验
    bool Enqueue(const std::wstring& url, const std::wstring& path,
                 const std::wstring& sha1 = L"", long long expectBytes = 0,
                 const std::vector<std::wstring>& mirrors = {});

    void Tick();

    xgdl::Progress Progress() const { return prog_; }
    std::vector<xgdl::Event> TakeEvents();

    bool Busy() const;

private:
    std::unique_ptr<xgdl::Manager> mgr_;
    xgdl::Progress prog_{};
    std::vector<xgdl::Event> events_;
};
