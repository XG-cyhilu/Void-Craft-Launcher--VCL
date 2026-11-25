#include "core/Markdown.h"
#include "core/Text.h"

#include "md4c.h"

namespace vcl {
namespace {

struct Collector {
    std::vector<MdBlock> out;
    int cur = -1;                 // 当前正在写的块，-1 表示没有
    int quote = 0;                // 引用嵌套层级
    int listDepth = 0;            // 列表嵌套层级
    std::vector<bool> ordered;    // 每层是否有序
    std::vector<int> counters;    // 每层序号
    bool inItem = false;

    void Start(MdKind kind, int level = 0, int indent = 0) {
        MdBlock b;
        b.kind = kind;
        b.level = level;
        b.indent = indent;
        out.push_back(std::move(b));
        cur = (int)out.size() - 1;
    }

    MdBlock& Cur() { return out[(size_t)cur]; }

    // 收尾：去掉尾部空白/空行；整块没内容就丢掉（比如空段落、空列表项）
    void Finish() {
        if (cur < 0) return;
        MdBlock& b = Cur();
        if (b.kind == MdKind::Rule) {
            cur = -1;
            return;
        }
        // 正文里的硬换行压成空格（由 UI 按宽度重排）；代码块保留换行
        if (b.kind != MdKind::Code) {
            std::wstring t;
            t.reserve(b.text.size());
            for (wchar_t ch : b.text) t.push_back(ch == L'\n' ? L' ' : ch);
            b.text = Trim(t);
        } else {
            while (!b.text.empty() && (b.text.back() == L'\n' || b.text.back() == L' ')) b.text.pop_back();
        }
        if (b.text.empty()) {
            out.pop_back();
            cur = -1;
            return;
        }
        cur = -1;
    }

    void Append(const std::wstring& s) {
        if (s.empty()) return;
        if (cur < 0) Start(MdKind::Paragraph);   // 兜底：有文字没块（比如紧跟在表格后的裸文本）
        Cur().text += s;
    }
};

int OnBlockEnter(MD_BLOCKTYPE type, void* detail, void* ud) {
    auto* c = (Collector*)ud;
    switch (type) {
    case MD_BLOCK_QUOTE:
        ++c->quote;
        break;
    case MD_BLOCK_UL:
        ++c->listDepth;
        c->ordered.push_back(false);
        c->counters.push_back(0);
        break;
    case MD_BLOCK_OL: {
        auto* d = (MD_BLOCK_OL_DETAIL*)detail;
        ++c->listDepth;
        c->ordered.push_back(true);
        c->counters.push_back((int)d->start);
        break;
    }
    case MD_BLOCK_LI: {
        c->inItem = true;
        bool ord = !c->ordered.empty() && c->ordered.back();
        int indent = c->listDepth > 0 ? c->listDepth - 1 : 0;
        int num = 0;
        if (!c->counters.empty()) num = c->counters.back()++;
        c->Start(ord ? MdKind::Ordered : MdKind::Bullet, num, indent);
        break;
    }
    case MD_BLOCK_H: {
        auto* d = (MD_BLOCK_H_DETAIL*)detail;
        c->Start(MdKind::Heading, (int)d->level);
        break;
    }
    case MD_BLOCK_CODE: {
        auto* d = (MD_BLOCK_CODE_DETAIL*)detail;
        c->Start(MdKind::Code);
        if (d->lang.text && d->lang.size > 0) {
            std::string lang((const char*)d->lang.text, (size_t)d->lang.size);
            c->Cur().lang = Utf8ToWide(lang);
        }
        break;
    }
    case MD_BLOCK_HR:
        c->Start(MdKind::Rule);
        break;
    case MD_BLOCK_P:
        // 列表项里的段落就是列表项本身，别再开一块
        if (!c->inItem) c->Start(c->quote > 0 ? MdKind::Quote : MdKind::Paragraph);
        break;
    case MD_BLOCK_TR:
        c->Start(MdKind::Paragraph);
        break;
    default:
        break;
    }
    return 0;
}

int OnBlockLeave(MD_BLOCKTYPE type, void* /*detail*/, void* ud) {
    auto* c = (Collector*)ud;
    switch (type) {
    case MD_BLOCK_QUOTE:
        if (c->quote > 0) --c->quote;
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (c->listDepth > 0) --c->listDepth;
        if (!c->ordered.empty()) c->ordered.pop_back();
        if (!c->counters.empty()) c->counters.pop_back();
        break;
    case MD_BLOCK_LI:
        c->inItem = false;
        c->Finish();
        break;
    case MD_BLOCK_H:
    case MD_BLOCK_CODE:
    case MD_BLOCK_P:
    case MD_BLOCK_TR:
        c->Finish();
        break;
    default:
        break;
    }
    return 0;
}

int OnSpanEnter(MD_SPANTYPE /*type*/, void* /*detail*/, void* /*ud*/) {
    // 图片不插占位标记：md4c 会把 alt 文本照常推过来（有 alt 就显示 alt，没有就忽略）
    return 0;
}

int OnSpanLeave(MD_SPANTYPE /*type*/, void* /*detail*/, void* /*ud*/) { return 0; }

int OnText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* ud) {
    auto* c = (Collector*)ud;
    switch (type) {
    case MD_TEXT_NORMAL:
    case MD_TEXT_CODE:
        c->Append(Utf8ToWide(std::string((const char*)text, (size_t)size)));
        break;
    case MD_TEXT_BR:
        c->Append(L"\n");
        break;
    case MD_TEXT_SOFTBR:
        c->Append(L" ");
        break;
    case MD_TEXT_HTML:
    case MD_TEXT_NULLCHAR:
    default:
        break;   // 内嵌 HTML 丢掉
    }
    return 0;
}

} // namespace

std::vector<MdBlock> ParseMarkdown(const std::wstring& src) {
    std::string utf8 = WideToUtf8(src);
    Collector c;
    if (utf8.empty()) return {};

    MD_PARSER p{};
    p.flags = MD_DIALECT_GITHUB;
    p.enter_block = OnBlockEnter;
    p.leave_block = OnBlockLeave;
    p.enter_span = OnSpanEnter;
    p.leave_span = OnSpanLeave;
    p.text = OnText;

    md_parse((const char*)utf8.data(), (int)utf8.size(), &p, &c);
    if (c.cur >= 0) c.Finish();
    return std::move(c.out);
}

} // namespace vcl
