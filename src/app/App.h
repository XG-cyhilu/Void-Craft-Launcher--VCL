#pragma once
#include <memory>

#include "ui/Common.h"
#include "core/Settings.h"
#include "core/AsyncHttp.h"
#include "core/Downloader.h"

namespace vcl {

class DownloadPage;
class SettingsPage;

// 外壳：左侧一级导航（主页 / 下载 / 设置），选中后展开二级导航；底部下载状态条。
class App {
public:
    App();
    ~App();

    int Run();

private:
    void Build(vcui::Widget& body);
    void InitTitleBar();
    void SyncTitleNav();
    void RebuildSidebar();
    void RebuildContent();
    void ShowPage(int page, int sub);
    void ApplyUi();
    void SaveSettings();

    Settings settings_;
    AsyncHttp http_;
    std::unique_ptr<Downloader> dl_;
    UiCtx ctx_;

    vcui::Surface* surface_ = nullptr;
    std::unique_ptr<vcui::Surface> ownedSurface_;
    vcui::Widget* body_ = nullptr;
    vcui::TitleBar* title_ = nullptr;   // 库创建的标题栏，母导航挂在它上面

    std::shared_ptr<vcui::Stack> sidebar_;
    std::shared_ptr<vcui::Stack> content_;
    std::shared_ptr<vcui::Label> footerStatus_;
    std::shared_ptr<vcui::Progress> footerBar_;
    std::shared_ptr<vcui::Label> footerSpeed_;

    std::unique_ptr<DownloadPage> download_;
    std::unique_ptr<SettingsPage> settingsPage_;

    int page_ = 1;   // 0 主页 / 1 下载 / 2 设置
    int sub_ = 0;
    int lastCompleted_ = 0;
};

} // namespace vcl
