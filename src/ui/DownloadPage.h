#pragma once
#include <memory>
#include <vector>
#include <string>

#include "ui/Common.h"
#include "core/McApi.h"
#include "core/ModrinthApi.h"

namespace vcl {

// 下载页：六个子导航。游戏走 BMCLAPI / Mojang，其余五类只走 Modrinth。
// 列表不分页容器：一屏一页（游戏 30 条 / 其余 20 条），每条自成一个卡片。
class DownloadPage {
public:
    struct State;

    explicit DownloadPage(UiCtx* ctx);

    // sub: 0 游戏 / 1 模组 / 2 整合包 / 3 资源包 / 4 光影包 / 5 数据包
    std::shared_ptr<vcui::Widget> Build(int sub);
    static std::vector<std::wstring> SubNav();

private:
    void BuildGame(const std::shared_ptr<State>& s, vcui::Stack& page);
    void BuildModrinth(const std::shared_ptr<State>& s, vcui::Stack& page);
    void LoadManifest(const std::shared_ptr<State>& s);
    void RenderVersions(const std::shared_ptr<State>& s);
    void LoadSearch(const std::shared_ptr<State>& s, int page);
    void InstallGame(const std::shared_ptr<State>& s, const McVersion& v);
    void ShowVersions(const MrHit& hit, const std::wstring& type);
    void UpdatePager(const std::shared_ptr<State>& s);
    void GoPage(const std::shared_ptr<State>& s, int delta);

    UiCtx* ctx_;
};

} // namespace vcl
