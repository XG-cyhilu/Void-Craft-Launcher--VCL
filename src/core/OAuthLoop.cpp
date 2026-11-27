#include "core/OAuthLoop.h"

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdlib>

#include "core/Text.h"

#pragma comment(lib, "ws2_32.lib")

namespace {

const char* kPageOk =
    "<!doctype html><meta charset=\"utf-8\"><title>授权完成</title>"
    "<body style=\"font-family:Segoe UI,Microsoft YaHei;padding:40px;color:#222\">"
    "<h2 style=\"margin:0 0 8px\">授权完成</h2>"
    "<p style=\"color:#666\">可以关闭这个页面，回到启动器继续。</p></body>";

const char* kPageFail =
    "<!doctype html><meta charset=\"utf-8\"><title>授权未完成</title>"
    "<body style=\"font-family:Segoe UI,Microsoft YaHei;padding:40px;color:#222\">"
    "<h2 style=\"margin:0 0 8px\">没有拿到授权码</h2>"
    "<p style=\"color:#666\">回到启动器看看具体原因。</p></body>";

// 从 /callback?code=xxx&state=yyy 里抓 code，顺手做一次 URL 解码
std::string UrlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int v = 0;
            if (sscanf_s(s.c_str() + i + 1, "%2x", &v) == 1) {
                out.push_back((char)v);
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') out.push_back(' ');
        else out.push_back(s[i]);
    }
    return out;
}

std::string QueryParam(const std::string& target, const std::string& key) {
    size_t q = target.find('?');
    if (q == std::string::npos) return {};
    std::string qs = target.substr(q + 1);
    size_t start = 0;
    while (start <= qs.size()) {
        size_t amp = qs.find('&', start);
        std::string kv = qs.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.substr(0, eq) == key) return UrlDecode(kv.substr(eq + 1));
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return {};
}

} // namespace

OAuthLoop::~OAuthLoop() { Stop(); }

bool OAuthLoop::Start(int port, const std::wstring& path, std::wstring* err) {
    Stop();

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        if (err) *err = L"网络初始化失败";
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        if (err) *err = L"创建监听套接字失败";
        WSACleanup();
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 1) != 0) {
        if (err) *err = WFormat(L"本地端口 %d 被占用（换一个端口或关掉占用它的程序）", port);
        closesocket(s);
        WSACleanup();
        return false;
    }

    port_ = (unsigned short)port;
    path_ = path;
    sock_ = (unsigned long long)s;
    {
        std::lock_guard<std::mutex> lk(m_);
        code_.clear();
        err_.clear();
    }
    done_ = false;
    running_ = true;
    th_ = std::thread([this] { Serve(); });
    return true;
}

void OAuthLoop::Serve() {
    SOCKET s = (SOCKET)sock_;
    SOCKET c = accept(s, nullptr, nullptr);
    std::string code;
    std::wstring err;

    if (c == INVALID_SOCKET) {
        err = L"授权回调监听已停止";
    } else {
        std::string req;
        char buf[2048];
        int n = 0;
        // 只读请求行就够：GET /callback?code=... HTTP/1.1
        while ((n = recv(c, buf, (int)sizeof(buf) - 1, 0)) > 0) {
            buf[n] = 0;
            req += buf;
            if (req.find("\r\n") != std::string::npos) break;
            if (req.size() > 4096) break;
        }
        std::string line = req.substr(0, req.find("\r\n"));
        size_t sp1 = line.find(' ');
        size_t sp2 = line.find(' ', sp1 + 1);
        std::string target = sp1 == std::string::npos
            ? std::string()
            : line.substr(sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1);

        code = QueryParam(target, "code");
        std::string errCode = QueryParam(target, "error");
        if (code.empty())
            err = errCode.empty() ? L"回调里没有 code" : WFormat(L"授权被拒绝：%s", Utf8ToWide(errCode).c_str());

        std::string body = code.empty() ? kPageFail : kPageOk;
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                           "Content-Length: " + std::to_string(body.size()) +
                           "\r\nConnection: close\r\n\r\n" + body;
        send(c, resp.c_str(), (int)resp.size(), 0);
        closesocket(c);
    }

    {
        std::lock_guard<std::mutex> lk(m_);
        code_ = Utf8ToWide(code);
        err_ = err;
    }
    done_ = true;
    running_ = false;
    closesocket(s);
    sock_ = 0;
}

bool OAuthLoop::Poll(std::wstring& code, std::wstring& err) {
    if (!done_.load()) return false;
    std::lock_guard<std::mutex> lk(m_);
    code = code_;
    err = err_;
    return true;
}

void OAuthLoop::Stop() {
    if (!running_.load() && !done_.load() && !th_.joinable()) return;
    if (sock_ != 0) {
        closesocket((SOCKET)sock_);   // 打断 accept
        sock_ = 0;
    }
    if (th_.joinable()) th_.join();
    running_ = false;
}
