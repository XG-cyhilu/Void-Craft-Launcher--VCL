#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

// 一次性本地回环监听：浏览器授权完会跳到 http://127.0.0.1:<port>/callback?code=...
// 这里接住那一次请求，取出 code 后回一个"可以关页面了"的小网页。
// 只监听 127.0.0.1，不做任何路由，跑完即停。
class OAuthLoop {
public:
    OAuthLoop() = default;
    ~OAuthLoop();

    bool Start(int port, const std::wstring& path, std::wstring* err = nullptr);

    // 非阻塞：拿到结果返回 true；code 为空表示失败，err 带原因
    bool Poll(std::wstring& code, std::wstring& err);

    void Stop();
    bool Running() const { return running_.load(); }

private:
    void Serve();

    std::thread th_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> done_{ false };
    std::mutex m_;
    std::wstring code_;
    std::wstring err_;
    unsigned short port_ = 0;
    std::wstring path_;
    unsigned long long sock_ = 0;
};
