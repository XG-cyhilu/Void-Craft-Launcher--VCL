#pragma once
#include <memory>
#include <vector>

#include "core/Markdown.h"

namespace vcui {
class Widget;
struct Tokens;
} // namespace vcui

namespace vcl {

// 把 ParseMarkdown 出来的块排成控件树：标题/正文/列表/代码/引用/分隔线各一套样式。
// 正文用按可用宽度自动折行的 Label，代码块放在浅底卡片里保留换行。
std::shared_ptr<vcui::Widget> BuildMarkdown(const std::vector<MdBlock>& blocks, const vcui::Tokens& t);

} // namespace vcl
