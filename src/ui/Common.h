#pragma once
#include <functional>
#include <memory>
#include <string>

#include "vcui/vcui.h"
#include "core/AsyncHttp.h"
#include "core/Downloader.h"
#include "core/Settings.h"

namespace vcl {

// 页面共享的上下文：网络、下载、配置与提示条
class IconStore;
class ModWiki;

struct UiCtx {
    vcui::Surface* surface = nullptr;
    AsyncHttp* http = nullptr;
    IconStore* icons = nullptr;
    ModWiki* wiki = nullptr;   // 汉化名 / 中文检索（resource\modwiki\ModWiki.json）
    Downloader* dl = nullptr;
    Settings* settings = nullptr;
    std::function<void()> saveSettings;
    std::function<void()> applyUi;      // 主题 / 强调色 / 不透明度变化后重刷界面
    std::function<void()> clearCache;   // 清会话缓存（刷新列表 / 换页时用）
    // 操作提示的去处：外壳把它接到状态栏，点了状态栏再弹 InfoBar；为空时直接弹
    std::function<void(const std::wstring&, vcui::InfoTone)> operation;
    void Toast(const std::wstring& text, vcui::InfoTone tone = vcui::InfoTone::Info);
};

std::shared_ptr<vcui::Label> Text(const std::wstring& s, const vcui::TypeStyle& st = vcui::Type::Body1);
std::shared_ptr<vcui::Label> Caption(const std::wstring& s);
std::shared_ptr<vcui::Stack> Row(float gap = 8.f);
std::shared_ptr<vcui::Stack> Col(float gap = 8.f);
std::shared_ptr<vcui::Widget> VSpace(float h);

// 页头：标题 + 说明
std::shared_ptr<vcui::Widget> PageHeader(const std::wstring& title, const std::wstring& sub);

// 选择文件夹（须在 UI 线程调用，会阻塞）
bool PickFolder(std::wstring& out);

// 每帧泵：网络回调 + 下载进度。Busy() 为真时让 Surface 保持 60fps。
class FramePump : public vcui::Widget {
public:
    std::function<bool()> Pump;     // 返回 true 表示还需要继续跑帧
    FramePump() { W = 0.f; H = 0.f; }
    D2D1_SIZE_F Desired(float, float) override { return D2D1::SizeF(0.f, 0.f); }
    bool Update(float dt) override {
        bool busy = Pump ? Pump() : false;
        if (busy) AnimDirty = true;   // 只保持帧循环；真正的重绘由状态变化 / 资源到达触发
        return busy;
    }
};

// 居中弹出面板（自绘覆盖层），带关闭按钮与淡入；contentOut 回填内容列
std::shared_ptr<vcui::Card> OpenPanel(vcui::Surface* host, const std::wstring& title,
                                      float width, float height, std::function<void()> onClose,
                                      std::shared_ptr<vcui::Stack>* contentOut = nullptr);

} // namespace vcl


