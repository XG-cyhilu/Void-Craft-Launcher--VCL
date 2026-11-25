#pragma once
#include <d2d1.h>
#include <wrl/client.h>

#include <map>
#include <memory>
#include <string>

#include "core/AsyncHttp.h"
#include "core/Image.h"

namespace vcui { class Surface; }

namespace vcl {

// 项目图标仓库：URL → 解码后的 D2D 位图。
// 三级缓存：内存位图 → 内存像素 → 磁盘原始字节；网络只在最后一级触发。
// 全部在 UI 线程取用，抓取由自己的 AsyncHttp 线程完成（不跟接口请求抢队）。
class IconStore {
public:
    explicit IconStore(vcui::Surface* surface);
    ~IconStore();

    // 命中返回位图；首次调用会排队抓取并返回 nullptr（下一帧再取）
    ID2D1Bitmap* Acquire(const std::wstring& url);

    // 每帧调用：推进网络回调、建位图
    void Pump();
    bool Busy() const;

private:
    struct Entry {
        std::shared_ptr<RgbaImage> img;                      // 已解码（直通 alpha）
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bmp;             // 已上传到 D2D
        ID2D1RenderTarget* owner = nullptr;                  // bmp 所属的渲染目标，设备重建后失效
        bool requested = false;
        bool failed = false;
    };

    Entry& At(const std::wstring& url);
    void LoadFromDisk(Entry& e, const std::wstring& url);
    bool EnsureBitmap(Entry& e);
    void CachePath(const std::wstring& url, std::wstring& out) const;

    vcui::Surface* surface_ = nullptr;
    AsyncHttp http_;
    std::map<std::wstring, std::unique_ptr<Entry>> items_;
    std::wstring cacheDir_;
};

} // namespace vcl

