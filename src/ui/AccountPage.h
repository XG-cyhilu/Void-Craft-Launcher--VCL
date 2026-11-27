#pragma once
#include <memory>

#include "ui/Common.h"

namespace vcl {

// 管理 · 账户：先做离线账户（玩家名 + 由名字按 Java 离线规则生成的 UUID）。
// 正版 Microsoft 登录还没接，卡片里只放说明与禁用入口。
class AccountPage {
public:
    explicit AccountPage(UiCtx* ctx);

    static std::vector<std::wstring> SubNav();
    std::shared_ptr<vcui::Widget> Build(int sub);

private:
    UiCtx* ctx_ = nullptr;
};

} // namespace vcl
