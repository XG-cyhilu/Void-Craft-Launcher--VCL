#include "core/AsyncHttp.h"
#include "core/Text.h"

#include "xgnet/xgnet.h"

AsyncHttp::AsyncHttp() { worker_ = std::thread([this] { Worker(); }); }

AsyncHttp::~AsyncHttp() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void AsyncHttp::Get(const std::wstring& url, Callback cb,
                    const std::vector<std::pair<std::wstring, std::wstring>>& headers,
                    int timeoutMs) {
    {
        std::lock_guard<std::mutex> lk(m_);
        jobs_.push(Job{ url, headers, timeoutMs, std::move(cb) });
        pending_++;
    }
    cv_.notify_one();
}

bool AsyncHttp::Busy() const { return pending_.load() > 0; }

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
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this] { return quit_ || !jobs_.empty(); });
            if (quit_ && jobs_.empty()) return;
            job = jobs_.front();
            jobs_.pop();
        }

        xgnet::Request req;
        req.url = WideToUtf8(job.url);
        req.userAgent = "XGstudio-VCL/0.1 (+https://github.com/XG-cyhilu/Void-Craft-Launcher--VCL)";
        req.headers.emplace_back("Accept", "application/json");
        for (const auto& h : job.headers)
            req.headers.emplace_back(WideToUtf8(h.first), WideToUtf8(h.second));
        req.timeoutMs = (uint32_t)(job.timeoutMs > 1000 ? job.timeoutMs : 1000);

        xgnet::Response res;
        bool ok = client.Perform(req, res) && res.IsSuccess();

        Done d;
        d.cb = std::move(job.cb);
        d.ok = ok;
        d.body = res.body;
        if (!ok) {
            if (!res.error.empty()) d.err = res.error;
            else d.err = "HTTP " + std::to_string(res.status);
        }
        {
            std::lock_guard<std::mutex> lk(dm_);
            done_.push_back(std::move(d));
        }
        pending_--;
    }
}

