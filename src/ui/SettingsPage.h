#pragma once
#include <memory>

#include "ui/Common.h"

namespace vcl {

// 设置页：界面设置 / 全局设置。改动即时落盘到 global.json。
class SettingsPage {
public:
    explicit SettingsPage(UiCtx* ctx);

    // sub: 0 界面设置 / 1 全局设置
    std::shared_ptr<vcui::Widget> Build(int sub);
    // 「更多 → 设置」用：顶部一个页签，界面设置 / 全局设置 叠在同一块区域里切换
    std::shared_ptr<vcui::Widget> BuildAll();
    static std::vector<std::wstring> SubNav();

private:
    struct State;
    void BuildUi(const std::shared_ptr<State>& s, vcui::Stack& page);
    void BuildGlobal(const std::shared_ptr<State>& s, vcui::Stack& page);
    void RenderRoots(const std::shared_ptr<State>& s);

    UiCtx* ctx_;
};

} // namespace vcl
