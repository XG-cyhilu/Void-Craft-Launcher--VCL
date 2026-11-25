#include "ui/AboutPage.h"

#include <windows.h>
#include <shellapi.h>

#include <functional>

namespace vcl {

namespace {

D2D1_COLOR_F Muted2(const UiCtx* ctx) {
    return ctx && ctx->surface ? ctx->surface->Th().t.text2 : vcui::Hex(0x606060);
}
D2D1_COLOR_F Muted3(const UiCtx* ctx) {
    return ctx && ctx->surface ? ctx->surface->Th().t.text3 : vcui::Hex(0x777777);
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

// 可点的纯文字链接：手型光标，点击即执行
class LinkText : public vcui::Label {
public:
    LinkText(const std::wstring& s, float size, D2D1_COLOR_F c) : Label(s, size, 400) { Color = c; }
    std::function<void()> Clicked;
    vcui::CursorShape CursorAt(float, float) const override { return vcui::CursorShape::Hand; }
    void OnClickAt(float, float) override {
        if (Clicked) Clicked();
    }
};
// 整块可点的行：和 mod 卡一样的手型 + 悬停变色，右侧带一个「↗」
class LinkRow : public vcui::Card {
public:
    std::function<void()> Clicked;
    LinkRow() {
        Shadow = false;
        Border = true;
        Radius = 6.f;
        Pad(10.f);
        Interactive = true;
    }
    vcui::CursorShape CursorAt(float, float) const override { return vcui::CursorShape::Hand; }
    void OnClickAt(float, float) override {
        if (Clicked) Clicked();
    }
    void Paint(vcui::PaintCtx& ctx) override {
        vcui::Card::Paint(ctx);
        float a = HoverA();
        if (a <= .001f) return;
        D2D1_RECT_F fr = Frame();
        fr.left += 1.f; fr.top += 1.f; fr.right -= 1.f; fr.bottom -= 1.f;
        D2D1_COLOR_F c = ctx.t.dark ? vcui::C(1, 1, 1, 1) : vcui::C(0, 0, 0, 1);
        c.a = a * (ctx.t.dark ? .11f : .06f);
        ctx.g.RoundRect(fr, Radius > 0.f ? Radius - 1.f : 0.f, c);
    }
};

// 一段说明文字：按可用宽度排版，多行时自己报出真实高度（基类只在 W>0 时量行数）
class WrapText : public vcui::Label {
public:
    WrapText(const std::wstring& s, float size, D2D1_COLOR_F color) : Label(s, size, 400) {
        Wrap = true;
        Color = color;
    }
    D2D1_SIZE_F Desired(float availW, float availH) override {
        if (W > 0.f || availW <= 0.f) return vcui::Label::Desired(availW, availH);
        W = availW;                                   // 临时定宽，量完立刻交还弹性
        D2D1_SIZE_F d = vcui::Label::Desired(availW, availH);
        W = 0.f;
        return d;
    }
};

std::shared_ptr<vcui::Label> Para(const std::wstring& s, float size, D2D1_COLOR_F color) {
    return std::make_shared<WrapText>(s, size, color);
}

// 小标题 + 卡片容器
std::shared_ptr<vcui::Widget> Section(const std::wstring& title, std::shared_ptr<vcui::Widget> body) {
    auto box = vcui::VBox(8.f);
    auto head = std::make_shared<vcui::Label>(title, 16.f, 400);
    head->MT = 22.f;
    box->Add(head);
    auto card = std::make_shared<vcui::Card>();
    card->Shadow = false;
    card->Border = true;
    card->Radius = 8.f;
    card->Pad(0.f);
    auto inner = vcui::VBox(0.f);
    inner->PL = 16.f;
    inner->PR = 16.f;
    inner->PT = 12.f;
    inner->PB = 12.f;
    inner->Add(body);
    card->Add(inner);
    box->Add(card);
    return box;
}

std::shared_ptr<vcui::Widget> RepoRow(const UiCtx* ctx, const std::wstring& name, const std::wstring& url) {
    auto row = std::make_shared<LinkRow>();
    auto line = vcui::HBox(8.f);
    line->Cross = vcui::Stack::Center;
    auto lb = std::make_shared<vcui::Label>(name, 14.f, 400);
    lb->Grow = 1.f;
    lb->Ellipsis = true;
    line->Add(lb);
    auto arrow = std::make_shared<vcui::SvgIcon>();
    arrow->Path = L"M5 12h13M12 6l6 6-6 6";   // 外链箭头（→ 加箭头尖）
    arrow->IconSize = 12.f;
    arrow->W = 12.f;
    arrow->H = 12.f;
    arrow->Stroke = 1.5f;
    arrow->Color = Muted3(ctx);
    line->Add(arrow);
    row->Add(line);
    row->Clicked = [url] { OpenUrl(url); };
    row->Tooltip = url;
    return row;
}

} // namespace

std::shared_ptr<vcui::Widget> AboutPage::Build(int sub) {
    auto page = std::make_shared<vcui::ScrollView>();
    auto root = vcui::VBox(14.f);
    root->PL = 4.f;
    root->PR = 8.f;
    root->PB = 24.f;

    if (sub == 1) {
        // ---------------- 许可 ----------------
        auto body = vcui::VBox(6.f);
        auto table = [&](const std::wstring& name, const std::wstring& lic, const std::wstring& note) {
            auto row = vcui::HBox(10.f);
            row->Cross = vcui::Stack::Center;
            row->MT = 4.f;
            row->MB = 4.f;
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
            body->Add(row);
            body->Add(std::make_shared<vcui::Divider>());
        };
        table(L"XGStudio-cpp-Core", L"Apache-2.0", L"下载 / 文件 / 线程基建");
        table(L"XGStudio-cpp-UI（VCUI）", L"Apache-2.0", L"自绘控件库");
        table(L"libwebp", L"BSD-3-Clause", L"WebP 图标解码");
        table(L"md4c", L"MIT", L"Markdown 解析");
        table(L"LightyLauncherLib", L"MIT", L"实例 / 启动后端");

        auto note = Para(L"完整登记表（版本、本地修改、同步记录）在仓库的 docs/THIRD_PARTY.md。"
                         L"以源码副本方式内置的依赖，其许可证原文随各自的 third_party 目录分发。",
                         13.f, Muted2(ctx_));
        note->MT = 10.f;
        body->Add(note);

        root->Add(Section(L"内置依赖与许可证", body));
        auto openRepo = std::make_shared<vcui::Button>(L"查看 THIRD_PARTY.md", false);
        openRepo->W = 190.f;
        openRepo->Clicked = [] {
            OpenUrl(L"https://github.com/XG-cyhilu/Void-Craft-Launcher--VCL/blob/main/docs/THIRD_PARTY.md");
        };
        root->Add(openRepo);
        page->Add(root);
        return page;
    }

    // ---------------- 关于 ----------------
    // 头部：名字 / 构建号 / Powered by
    auto heroCard = std::make_shared<vcui::Card>();
    heroCard->Shadow = false;
    heroCard->Border = true;
    heroCard->Radius = 8.f;
    heroCard->Pad(0.f);
    auto hero = vcui::VBox(4.f);
    hero->PL = 20.f;
    hero->PR = 20.f;
    hero->PT = 18.f;
    hero->PB = 18.f;
    auto name = std::make_shared<vcui::Label>(L"Void Craft Launcher", 26.f, 400);
    hero->Add(name);
    auto build = std::make_shared<vcui::Label>(L"-xg26a01a", 12.f, 400);
    build->Color = Muted3(ctx_);
    hero->Add(build);
    auto powered = std::make_shared<vcui::Label>(L"Powered by XG STUDIO", 13.f, 400);
    powered->Color = Muted2(ctx_);
    powered->MT = 6.f;
    hero->Add(powered);
    heroCard->Add(hero);
    root->Add(heroCard);

    // 开源项目致谢
    auto repos = vcui::VBox(6.f);
    repos->Add(RepoRow(ctx_, L"XG-cyhilu / XGStudio-cpp-Core", L"https://github.com/XG-cyhilu/XGStudio-cpp-Core"));
    repos->Add(RepoRow(ctx_, L"XG-cyhilu / XGStudio-cpp-UI", L"https://github.com/XG-cyhilu/XGStudio-cpp-UI"));
    repos->Add(RepoRow(ctx_, L"Meloong-Git / PCL", L"https://github.com/Meloong-Git/PCL"));
    repos->Add(RepoRow(ctx_, L"webmproject / libwebp", L"https://github.com/webmproject/libwebp"));
    repos->Add(RepoRow(ctx_, L"mity / md4c", L"https://github.com/mity/md4c"));
    root->Add(Section(L"开源项目致谢", repos));

    // 个人 / 群体致谢
    auto persons = vcui::VBox(6.f);
    auto personLine = [this](const std::wstring& a, const std::wstring& b, const std::wstring& c) {
        auto line = vcui::HBox(16.f);
        for (const auto* s : { &a, &b, &c }) {
            auto lb = std::make_shared<vcui::Label>(*s, 14.f, 400);
            lb->Color = Muted2(ctx_);
            lb->Grow = 1.f;
            lb->Ellipsis = true;
            line->Add(lb);
        }
        return line;
    };
    persons->Add(personLine(L"Bangbang93 / BMCLAPI", L"Rinth / Modrinth API", L"OMS Work! / SkyCraftLauncher"));
    persons->Add(personLine(L"LittleSkin / OAuth", L"全体官方群成员", L"Minecraft 社区"));
    root->Add(Section(L"个人 / 群体致谢", persons));

    // 免责声明
    auto legal = vcui::VBox(10.f);
    legal->Add(Para(L"Minecraft 是 MOJANG 与 MICROSOFT 的商标。XG STUDIO 及其项目与 MOJANG、MICROSOFT "
                    L"无隶属、赞助、关联或背书关系，也未经 MOJANG 或 MICROSOFT 批准，不分发任何账户数据；"
                    L"启动器仅按公开规范读取用户自有目录且不进行无不必要的网络活动。",
                    13.f, Muted2(ctx_)));
    legal->Add(Para(L"Minecraft is a trademark of MOJANG and MICROSOFT. XG STUDIO and its projects have no "
                    L"affiliation, sponsorship, association, or endorsement from MOJANG or MICROSOFT, and are not "
                    L"approved by MOJANG or MICROSOFT. They do not distribute any account data; the "
                    L"launcher only reads the user's own directory according to public specifications and "
                    L"does not perform any unnecessary network activities.",
                    12.f, Muted3(ctx_)));
    root->Add(Section(L"免责声明", legal));

    // 页脚
    auto foot = vcui::VBox(8.f);
    auto links = vcui::HBox(6.f);
    links->Cross = vcui::Stack::Center;
    auto repoLink = std::make_shared<LinkText>(L"Void Craft Launcher 是一个开源项目", 13.f,
                                               ctx_ && ctx_->surface ? ctx_->surface->Th().t.accent : vcui::Hex(0x0071EA));
    repoLink->Clicked = [] { OpenUrl(L"https://github.com/XG-cyhilu/Void-Craft-Launcher--VCL"); };
    repoLink->Grow = 1.f;
    links->Add(repoLink);

    foot->Add(links);

    auto qq = vcui::HBox(6.f);
    qq->Cross = vcui::Stack::Center;
    auto qqText = std::make_shared<LinkText>(L"官方 QQ 群 903815517（点击复制）", 13.f, Muted2(ctx_));
    qqText->Clicked = [self = this] {
        CopyText(L"903815517");
        if (self->ctx_) self->ctx_->Toast(L"已复制群号 903815517", vcui::InfoTone::Success);
    };
    qqText->Grow = 1.f;
    qq->Add(qqText);
    foot->Add(qq);

    foot->Add(std::make_shared<vcui::Divider>());
    auto studio = std::make_shared<vcui::Label>(L"XG STUDIO", 15.f, 400);
    foot->Add(studio);
    auto motto = std::make_shared<vcui::Label>(L"Thinking never ends. | 思考永无止境。", 12.f, 400);
    motto->Color = Muted3(ctx_);
    foot->Add(motto);
    auto cr = std::make_shared<vcui::Label>(L"Void Craft Launcher (VCL) · Copyright (C) 2026 XG STUDIO", 12.f, 400);
    cr->Color = Muted3(ctx_);
    foot->Add(cr);
    root->Add(foot);

    page->Add(root);
    return page;
}

} // namespace vcl
