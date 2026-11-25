#pragma once
#include <memory>
#include <vector>

#include "ui/Common.h"
#include "core/Settings.h"
#include "core/AsyncHttp.h"
#include "core/Downloader.h"

namespace vcl {

class IconStore;
class ModWiki;
class DownloadPage;
class SettingsPage;

// 状态栏片段：一行小字，自己算宽高；Clickable 时手型光标 + 悬停底色
class StatusText : public vcui::Widget {
public:
    std::wstring Text;
    float Size = 12.f;
    int Weight = 400;
    vcui::HAlign Align = vcui::HAlign::Left;
    bool Clickable = false;
    float Life = -1.f;      // >=0 时倒计时，到 0 淡出清空（"最近操作"用它自动退场）
    D2D1_COLOR_F Ink{ 0, 0, 0, 0 };   // alpha>0 时覆盖默认文字色（强调色状态栏用反色）

    explicit StatusText(std::wstring text);
    D2D1_SIZE_F Desired(float availW, float availH) override;
    vcui::CursorShape CursorAt(float, float) const override;
    bool Update(float dt) override;
    void Paint(vcui::PaintCtx& ctx) override;
};

// 外壳：左侧一级导航（主页 / 下载 / 设置），选中后展开二级导航；底部下载状态条。
class App {
public:
    App();
    ~App();

    int Run();

private:
    void Build(vcui::Widget& body);
    void BuildStatusBar(vcui::Widget& body);
    void ApplyStatusColors();     // 状态栏底色 + 文字色（跟随主题 / 强调色）
    void SetOperation(const std::wstring& text, vcui::InfoTone tone);
    void InitTitleBar();
    void SyncTitleNav();
    void RebuildSidebar();
    void UpdateSidebarActive();
    void RebuildContent();
    std::shared_ptr<vcui::Widget> BuildPlaceholder(const std::wstring& title,
                                                   const std::vector<std::wstring>& items);
    void ShowPage(int page, int sub);
    void ApplyUi();
    void SaveSettings();
    void ClearNetCache();      // 清掉会话级磁盘缓存（JSON 响应 + 图标）

    Settings settings_;
    AsyncHttp http_;
    std::unique_ptr<Downloader> dl_;
    std::unique_ptr<IconStore> icons_;
    std::unique_ptr<ModWiki> wiki_;
    UiCtx ctx_;

    vcui::Surface* surface_ = nullptr;
    std::unique_ptr<vcui::Surface> ownedSurface_;
    vcui::Widget* body_ = nullptr;
    vcui::TitleBar* title_ = nullptr;   // 库创建的标题栏，母导航挂在它上面

    std::shared_ptr<vcui::Stack> sidebar_;
    std::shared_ptr<vcui::Widget> sidebarLine_;   // 侧栏右缘：分隔线 + 向右淡出的投影
    std::vector<std::shared_ptr<vcui::NavItem>> navItems_;
    std::shared_ptr<vcui::Stack> content_;
    std::shared_ptr<vcui::Stack> statusBar_;     // 状态栏本体（主题切换时改底色）
    // 底部状态栏：状态 / 账户 / 最近操作 / 任务 + 右对齐的 Minecraft 目录
    std::shared_ptr<StatusText> statusState_;
    std::shared_ptr<StatusText> statusUser_;
    std::shared_ptr<StatusText> statusOp_;
    std::shared_ptr<StatusText> statusTask_;
    std::shared_ptr<StatusText> statusDir_;
    std::wstring lastRequestText_;               // "正在请求 xxx"
    std::wstring opText_;
    vcui::InfoTone opTone_ = vcui::InfoTone::Info;

    std::unique_ptr<DownloadPage> download_;
    std::unique_ptr<SettingsPage> settingsPage_;

    int page_ = 2;   // 0 主页 / 1 版本 / 2 下载 / 3 管理 / 4 更多
    int sub_ = 0;
    int lastCompleted_ = 0;
};

} // namespace vcl



