#pragma once
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>

// 后台线程跑 xgnet 同步请求，结果回到 UI 线程（Pump 里回调）。
// 这样界面永远不会被网络阻塞。
class AsyncHttp {
public:
    using Callback = std::function<void(bool ok, const std::string& body, const std::string& err)>;

    AsyncHttp();
    ~AsyncHttp();

    // 必须在 UI 线程调用；cb 也在 UI 线程执行
    void Get(const std::wstring& url, Callback cb,
             const std::vector<std::pair<std::wstring, std::wstring>>& headers = {},
             int timeoutMs = 25000);

    // 后台线程每次把结果放进队列后调用（用来踢醒睡着的帧循环）
    // 注意：运行在 worker 线程，回调里只该做线程安全的唤醒动作
    std::function<void()> onWake;

    // 发起请求时（UI 线程）回调一次，给状态栏显示"正在请求 xxx"
    std::function<void(const std::wstring& url)> onRequest;

    // 当前有没有在途请求（Pump 用它决定要不要继续醒着）
    bool Busy() const;

    // UI 线程每帧调用：派发已完成的回调；返回是否执行过回调
    bool Pump();

private:
    struct Job {
        std::wstring url;
        std::vector<std::pair<std::wstring, std::wstring>> headers;
        int timeoutMs = 25000;
        Callback cb;
    };
    struct Done {
        Callback cb;
        bool ok = false;
        std::string body;
        std::string err;
    };

    void Worker();

    std::thread worker_;
    std::mutex m_;
    std::condition_variable cv_;
    std::queue<Job> jobs_;
    std::atomic<bool> quit_{ false };
    std::atomic<int> pending_{ 0 };

    std::mutex dm_;
    std::vector<Done> done_;
};

