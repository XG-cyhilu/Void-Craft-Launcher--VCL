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
    static std::vector<std::wstring> SubNav();

private:
    struct State;
    void BuildUi(const std::shared_ptr<State>& s, vcui::Stack& page);
    void BuildGlobal(const std::shared_ptr<State>& s, vcui::Stack& page);
    void RenderRoots(const std::shared_ptr<State>& s);

    UiCtx* ctx_;
};

} // namespace vcl
