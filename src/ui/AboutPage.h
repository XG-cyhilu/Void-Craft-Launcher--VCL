#pragma once
#include <memory>

#include "ui/Common.h"

namespace vcl {

// 「关于」页：版本信息、致谢、免责声明与页脚。
// sub = 0 关于 / 1 许可（内置依赖与许可证一览）
class AboutPage {
public:
    explicit AboutPage(UiCtx* ctx) : ctx_(ctx) {}
    std::shared_ptr<vcui::Widget> Build(int sub);

private:
    UiCtx* ctx_;
};

} // namespace vcl
