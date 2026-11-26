#include "ui/AboutPage.h"

#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <vector>

namespace vcl {

namespace {

D2D1_COLOR_F Muted2(const UiCtx* ctx) {
    return ctx && ctx->surface ? ctx->surface->Th().t.text2 : vcui::Hex(0x606060);
}
D2D1_COLOR_F Muted3(const UiCtx* ctx) {
    return ctx && ctx->surface ? ctx->surface->Th().t.text3 : vcui::Hex(0x777777);
}
D2D1_COLOR_F AccentOf(const UiCtx* ctx) {
    return ctx && ctx->surface ? ctx->surface->Th().t.accent : vcui::Hex(0x0071EA);
}

void OpenUrl(const std::wstring& url) {
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void CopyText(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (h) {
        void* p = GlobalLock(h);
        if (p) {
            memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
    }
    CloseClipboard();
}

// ---- 小胶囊：致谢名单用，比一条一行省空间，也比卡片套卡片干净 ----
class Pill : public vcui::Widget {
public:
    std::wstring Text;
    float Size = 13.f;
    D2D1_COLOR_F TextColor{ 0, 0, 0, 0 };   // a == 0 时按可点/不可点取主题色
    std::function<void()> Clicked;

    Pill(const std::wstring& s, float size, bool clickable) : Text(s), Size(size) {
        Interactive = clickable;
        if (clickable) Tooltip = L"打开仓库";
    }
    D2D1_SIZE_F Desired(float, float) override {
        return D2D1::SizeF(Tw(Text, Size, 400) + 26.f, Size + 17.f);
    }
    vcui::CursorShape CursorAt(float, float) const override {
        return Interactive ? vcui::CursorShape::Hand : vcui::CursorShape::Arrow;
    }
    void OnClickAt(float, float) override {
        if (Clicked) Clicked();
    }
    void Paint(vcui::PaintCtx& ctx) override {
        const auto& t = ctx.t;
        float a = HoverA();
        D2D1_COLOR_F bg = t.dark ? vcui::C(1, 1, 1, .055f) : vcui::C(0, 0, 0, .035f);
        if (Interactive && a > .001f) bg.a += a * (t.dark ? .05f : .045f);
        D2D1_RECT_F fr = Frame();
        ctx.g.RoundRect(fr, (fr.bottom - fr.top) * .5f, bg);
        D2D1_COLOR_F col = TextColor.a > 0.f ? TextColor : (Interactive ? t.wz : t.text2);
        ctx.g.Text(fr, Text, Size, 400, col, vcui::HAlign::Center, vcui::VAlign::Middle);
    }
};

std::shared_ptr<vcui::Widget> CenteredRow(const std::vector<std::shared_ptr<vcui::Widget>>& items, float gap = 8.f) {
    auto line = vcui::HBox(gap);
    line->Cross = vcui::Stack::Center;
    auto lead = vcui::Spacer(0.f);
    lead->Grow = 1.f;
    line->Add(lead);
    for (auto& it : items) line->Add(it);
    auto tail = vcui::Spacer(0.f);
    tail->Grow = 1.f;
    line->Add(tail);
    return line;
}

std::shared_ptr<vcui::Widget> Pill3(const std::wstring& a, const std::wstring& b, const std::wstring& c) {
    std::vector<std::shared_ptr<vcui::Widget>> row;
    row.push_back(std::make_shared<Pill>(a, 13.f, false));
    row.push_back(std::make_shared<Pill>(b, 13.f, false));
    if (!c.empty()) row.push_back(std::make_shared<Pill>(c, 13.f, false));
    return CenteredRow(row);
}

// 分类小标题：居中、淡色
std::shared_ptr<vcui::Label> SectionTitle(const UiCtx* ctx, const std::wstring& s) {
    auto l = std::make_shared<vcui::Label>(s, 13.f, 400);
    l->Color = Muted3(ctx);
    l->Align = vcui::HAlign::Center;
    l->MT = 16.f;
    return l;
}

// 定宽居中的段落（免责声明这种长文本不该铺满整个窗口）
std::shared_ptr<vcui::Widget> CenteredPara(const std::wstring& s, float size, D2D1_COLOR_F color, float width) {
    auto para = std::make_shared<vcui::Label>(s, size, 400);
    para->Wrap = true;
    para->Color = color;
    para->Align = vcui::HAlign::Center;
    para->W = width;
    return CenteredRow({ para }, 0.f);
}

// 长段文字：按可用宽度排版，多行时自己报出真实高度（基类只在 W>0 时量行数）
class WrapLabel : public vcui::Label {
public:
    WrapLabel(const std::wstring& s, float size, D2D1_COLOR_F c) : Label(s, size, 400) {
        Wrap = true;
        Color = c;
    }
    D2D1_SIZE_F Desired(float availW, float availH) override {
        if (W > 0.f || availW <= 0.f) return vcui::Label::Desired(availW, availH);
        W = availW;
        D2D1_SIZE_F d = vcui::Label::Desired(availW, availH);
        W = 0.f;
        return d;
    }
};

std::shared_ptr<vcui::Widget> WrapPara(const std::wstring& s, float size, D2D1_COLOR_F c) {
    return std::make_shared<WrapLabel>(s, size, c);
}
// 可点的纯文字（页脚用）
class LinkText : public vcui::Label {
public:
    LinkText(const std::wstring& s, float size, D2D1_COLOR_F c) : Label(s, size, 400) {
        Color = c;
        Interactive = true;       // 非 Interactive 的 Label 会被 Hit 上溯，永远收不到点击
    }
    std::function<void()> Clicked;
    vcui::CursorShape CursorAt(float, float) const override { return vcui::CursorShape::Hand; }
    void OnClickAt(float, float) override {
        if (Clicked) Clicked();
    }
};

// 细的强调色短横线（Hero 下方）
std::shared_ptr<vcui::Widget> AccentRule(const UiCtx* ctx, float w) {
    auto bar = std::make_shared<vcui::Card>();
    bar->Shadow = false;
    bar->Border = false;
    bar->Radius = 1.f;
    bar->Fill = AccentOf(ctx);
    bar->W = w;
    bar->H = 2.f;
    auto line = vcui::HBox(0.f);
    line->Cross = vcui::Stack::Center;
    auto lead = vcui::Spacer(0.f);
    lead->Grow = 1.f;
    line->Add(lead);
    line->Add(bar);
    auto tail = vcui::Spacer(0.f);
    tail->Grow = 1.f;
    line->Add(tail);
    return line;
}

} // namespace

std::shared_ptr<vcui::Widget> AboutPage::Build(int sub) {
    auto page = std::make_shared<vcui::ScrollView>();
    auto root = vcui::VBox(8.f);
    root->PL = 24.f;
    root->PR = 24.f;
    root->PB = 28.f;

    if (sub == 1) {
        // ---------------- 许可 ----------------
        auto head = std::make_shared<vcui::Label>(L"内置依赖与许可证", 16.f, 400);
        head->MT = 18.f;
        root->Add(head);

        auto card = std::make_shared<vcui::Card>();
        card->Shadow = false;
        card->Border = true;
        card->Radius = 8.f;
        card->Pad(0.f);
        auto inner = vcui::VBox(0.f);
        inner->PL = 16.f;
        inner->PR = 16.f;
        inner->PT = 10.f;
        inner->PB = 12.f;
        auto table = [&](const std::wstring& name, const std::wstring& lic, const std::wstring& note) {
            auto row = vcui::HBox(10.f);
            row->Cross = vcui::Stack::Center;
            row->MT = 5.f;
            row->MB = 5.f;
            auto n = std::make_shared<vcui::Label>(name, 14.f, 400);
            n->Grow = 1.f;
            n->Ellipsis = true;
            row->Add(n);
            if (!note.empty()) {
                auto nt = std::make_shared<vcui::Label>(note, 12.f, 400);
                nt->Color = Muted3(ctx_);
                nt->W = 150.f;
                nt->Ellipsis = true;
                row->Add(nt);
            }
            auto l = std::make_shared<vcui::Label>(lic, 12.f, 400);
            l->Color = Muted2(ctx_);
            l->W = 96.f;
            l->Align = vcui::HAlign::Right;
            row->Add(l);
            inner->Add(row);
            inner->Add(std::make_shared<vcui::Divider>());
        };
        table(L"XGStudio-cpp-Core", L"Apache-2.0", L"下载 / 文件 / 线程基建");
        table(L"XGStudio-cpp-UI（VCUI）", L"Apache-2.0", L"自绘控件库");
        table(L"libwebp", L"BSD-3-Clause", L"WebP 图标解码");
        table(L"md4c", L"MIT", L"Markdown 解析");
        table(L"LightyLauncherLib", L"MIT", L"实例 / 启动后端");

        auto note = std::make_shared<vcui::Label>(
            L"完整登记表（版本、本地修改、同步记录）在仓库的 docs/THIRD_PARTY.md；"
            L"以源码副本方式内置的依赖，其许可证原文随各自的 third_party 目录分发。",
            12.f, 400);
        note->Wrap = true;
        note->W = 560.f;
        note->Color = Muted3(ctx_);
        note->Align = vcui::HAlign::Center;
        note->MT = 12.f;
        note->MB = 4.f;
        inner->Add(note);
        card->Add(inner);
        root->Add(card);

        auto openRepo = std::make_shared<vcui::Button>(L"查看 THIRD_PARTY.md", false);
        openRepo->W = 190.f;
        openRepo->Clicked = [] {
            OpenUrl(L"https://github.com/XG-cyhilu/Void-Craft-Launcher--VCL/blob/main/docs/THIRD_PARTY.md");
        };
        auto btnLine = CenteredRow({ openRepo }, 0.f);
        btnLine->MT = 6.f;
        root->Add(btnLine);

        page->Add(root);
        return page;
    }

    // ---------------- 关于 ----------------
    // 纯文本：不加底色、不做胶囊、不套卡片
    auto name = std::make_shared<vcui::Label>(L"Void Craft Launcher", 24.f, 400);
    name->MT = 20.f;
    root->Add(name);

    auto tagline = std::make_shared<vcui::Label>(L"-xg26a01a · Powered by XG STUDIO", 12.f, 400);
    tagline->Color = Muted3(ctx_);
    tagline->MT = 4.f;
    root->Add(tagline);

    // 开源项目致谢
    auto t1 = std::make_shared<vcui::Label>(L"开源项目致谢", 12.f, 400);
    t1->Color = Muted3(ctx_);
    t1->MT = 22.f;
    root->Add(t1);

    auto repoCell = [&](const std::wstring& repo, const std::wstring& url) {
        auto lb = std::make_shared<LinkText>(repo, 13.f, D2D1::ColorF(0, 0, 0, 0));
        lb->Grow = 1.f;
        lb->Ellipsis = true;
        lb->Tooltip = url;
        lb->Clicked = [url] { OpenUrl(url); };
        return lb;
    };
    auto repoGrid = [&](std::shared_ptr<vcui::Widget> a, std::shared_ptr<vcui::Widget> b) {
        auto line = vcui::HBox(12.f);
        line->Cross = vcui::Stack::Center;
        line->Add(a);
        line->Add(b);
        return line;
    };
    root->Add(repoGrid(repoCell(L"XG-cyhilu / XGStudio-cpp-Core", L"https://github.com/XG-cyhilu/XGStudio-cpp-Core"),
                       repoCell(L"XG-cyhilu / XGStudio-cpp-UI", L"https://github.com/XG-cyhilu/XGStudio-cpp-UI")));
    root->Add(repoGrid(repoCell(L"Meloong-Git / PCL", L"https://github.com/Meloong-Git/PCL"),
                       repoCell(L"webmproject / libwebp", L"https://github.com/webmproject/libwebp")));
    root->Add(repoGrid(repoCell(L"mity / md4c", L"https://github.com/mity/md4c"),
                       std::make_shared<vcui::Widget>()));

    // 个人 / 群体致谢
    auto t2 = std::make_shared<vcui::Label>(L"个人 / 群体致谢", 12.f, 400);
    t2->Color = Muted3(ctx_);
    t2->MT = 20.f;
    root->Add(t2);

    auto plainLine = [&](const std::wstring& a, const std::wstring& b) {
        auto line = vcui::HBox(12.f);
        line->Cross = vcui::Stack::Center;
        for (const auto* s : { &a, &b }) {
            auto lb = std::make_shared<vcui::Label>(*s, 13.f, 400);
            lb->Color = Muted2(ctx_);
            lb->Grow = 1.f;
            lb->Ellipsis = true;
            line->Add(lb);
        }
        return line;
    };
    root->Add(plainLine(L"Bangbang93 / BMCLAPI", L"Rinth / Modrinth API"));
    root->Add(plainLine(L"OMS Work! / SkyCraftLauncher", L"LittleSkin / OAuth"));
    root->Add(plainLine(L"全体官方群成员", L"Minecraft 社区"));

    // 免责声明
    auto t3 = std::make_shared<vcui::Label>(L"免责声明", 12.f, 400);
    t3->Color = Muted3(ctx_);
    t3->MT = 20.f;
    root->Add(t3);

    // 免责声明：内容与 resource/XGSTUDFMC.txt 一致
    auto d1 = WrapPara(
        L"Minecraft is a trademark of Mojang Synergies AB (a Microsoft company). XG STUDIO and its projects "
        L"are not an official Minecraft product, are not affiliated with, sponsored by, or endorsed by "
        L"Mojang or Microsoft, and are not approved by either party.",
        12.5f, Muted2(ctx_));
    d1->MT = 6.f;
    root->Add(d1);

    auto d2 = WrapPara(
        L"XG STUDIO does not distribute any game data or account credentials. The launcher only reads and "
        L"writes files in the user's own .minecraft directory following publicly documented formats and "
        L"protocols, and performs no network activity beyond what is strictly required to fetch game assets "
        L"and mod metadata from public sources.",
        12.5f, Muted2(ctx_));
    d2->MT = 6.f;
    root->Add(d2);

    auto d3 = WrapPara(
        L"All game content belongs to its respective rights holders. Use of this software implies acceptance "
        L"of the Minecraft EULA (https://aka.ms/MinecraftEULA).",
        12.5f, Muted2(ctx_));
    d3->MT = 6.f;
    root->Add(d3);
    // 页脚（居中）
    auto gap = vcui::Spacer(16.f);
    root->Add(gap);

    auto repoLink = std::make_shared<LinkText>(L"Void Craft Launcher 是一个开源项目", 13.f, D2D1::ColorF(0, 0, 0, 0));
    repoLink->Clicked = [] { OpenUrl(L"https://github.com/XG-cyhilu/Void-Craft-Launcher--VCL"); };
    repoLink->Align = vcui::HAlign::Center;
    repoLink->Tooltip = L"https://github.com/XG-cyhilu/Void-Craft-Launcher--VCL";
    root->Add(repoLink);

    auto qqText = std::make_shared<LinkText>(L"官方 QQ 群 903815517（点击复制）", 13.f, Muted2(ctx_));
    qqText->Clicked = [self = this] {
        CopyText(L"903815517");
        if (self->ctx_) self->ctx_->Toast(L"已复制群号 903815517", vcui::InfoTone::Success);
    };
    qqText->Align = vcui::HAlign::Center;
    qqText->Tooltip = L"点击复制群号";
    root->Add(qqText);

    auto studio = std::make_shared<vcui::Label>(L"XG STUDIO", 14.f, 400);
    studio->Align = vcui::HAlign::Center;
    studio->MT = 10.f;
    root->Add(studio);
    auto motto = std::make_shared<vcui::Label>(L"Thinking never ends. | 思考永无止境。", 12.f, 400);
    motto->Color = Muted3(ctx_);
    motto->Align = vcui::HAlign::Center;
    root->Add(motto);
    auto cr = std::make_shared<vcui::Label>(L"Void Craft Launcher (VCL) · Copyright (C) 2026 XG STUDIO", 12.f, 400);
    cr->Color = Muted3(ctx_);
    cr->Align = vcui::HAlign::Center;
    root->Add(cr);
    page->Add(root);
    return page;
}

} // namespace vcl
