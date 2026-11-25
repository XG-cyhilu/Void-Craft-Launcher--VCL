#pragma once
#include <string>
#include <vector>

namespace vcl {

// 块级类型。行内加粗/斜体/行内代码的标记由 md4c 消化掉，这里只留纯文本 + 块级信息，
// 由 UI 层按块排版（标题/正文/列表/代码/引用各一套字号与间距）。
enum class MdKind { Heading, Paragraph, Quote, Bullet, Ordered, Code, Rule };

struct MdBlock {
    MdKind kind = MdKind::Paragraph;
    int level = 0;          // 标题级别 1..6；有序列表的序号
    int indent = 0;         // 列表嵌套层级（0 为顶层）
    std::wstring text;
    std::wstring lang;      // 代码块语言标记（```c 里的 c）
};

// 用 md4c（GitHub 方言）把 Modrinth 的 body 解析成块序列。
// 图片按 "[图片]" 占位，内嵌 HTML 直接丢弃（Modrinth 描述里常见 <center> 之类，留着是噪声）。
std::vector<MdBlock> ParseMarkdown(const std::wstring& src);

} // namespace vcl
