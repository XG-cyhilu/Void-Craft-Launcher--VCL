#pragma once
#include <d2d1.h>
#include <wrl/client.h>

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "core/AsyncHttp.h"
#include "core/Image.h"

namespace vcui { class Surface; }

namespace vcl {

// 项目图标仓库：URL → 解码后的 D2D 位图。
// 抓取、解码、落盘全在后台线程；UI 线程只做两件轻活：
//   1) 拿到解码结果后上传成 ID2D1Bitmap；2) 触发重绘。
// 渲染顺序：搜索请求 → JSON → 建卡片（先占位块）→ 后台抓图解码 → 下一帧补上图标。
class IconStore {
public:
    explicit IconStore(vcui::Surface* surface);
    ~IconStore();

    // 命中返回位图；未就绪返回 nullptr，同时确保后台已经在处理。
    // url 以 "file:" 开头时按本地文件读（resource 里自带的图标走这条路），其余按网络 URL。
    ID2D1Bitmap* Acquire(const std::wstring& url);

    // 后台有进展时踢醒帧循环（由 App 注入，跟 AsyncHttp::onWake 同一套动作）
    void SetWake(std::function<void()> fn);

    // 每帧调用：派发网络回调、吸收解码结果、按需上传位图
    void Pump();
    bool Busy();

private:
    struct Entry {
        std::shared_ptr<RgbaImage> img;                      // 已解码（直通 alpha）
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bmp;             // 已上传到 D2D
        ID2D1RenderTarget* owner = nullptr;                  // bmp 所属的渲染目标，设备重建后失效
        bool requested = false;
        bool failed = false;
    };

    struct Job {
        std::wstring url;
        std::vector<unsigned char> bytes;   // 网络回来直接带字节；磁盘命中时为空
        std::wstring localPath;             // "file:" 源时的绝对路径
        bool fromDisk = false;
    };
    struct Result {
        std::wstring url;
        std::shared_ptr<RgbaImage> img;
        bool failed = false;
        bool fromDisk = false;
        bool fromLocal = false;
    };

    Entry& At(const std::wstring& url);
    bool EnsureBitmap(Entry& e);
    void CachePath(const std::wstring& url, std::wstring& out) const;
    void Enqueue(Job job);
    void DrainResults();
    void Worker();

    vcui::Surface* surface_ = nullptr;
    AsyncHttp http_;
    std::map<std::wstring, std::unique_ptr<Entry>> items_;
    std::wstring cacheDir_;

    std::thread worker_;
    std::mutex jm_;
    std::condition_variable jcv_;
    std::queue<Job> jobs_;
    bool quit_ = false;

    std::mutex rm_;
    std::vector<Result> results_;
    std::function<void()> wake_;
};

} // namespace vcl


