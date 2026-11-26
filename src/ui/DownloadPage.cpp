#include "ui/DownloadPage.h"
#include "ui/IconImage.h"
#include "ui/MarkdownView.h"
#include "core/Markdown.h"
#include "core/Paths.h"
#include "core/ModWiki.h"
#include "core/Text.h"

#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <algorithm>

#include <windows.h>
#include <shellapi.h>

namespace vcl {

struct DownloadPage::State {
    std::shared_ptr<vcui::Stack> list;
    std::shared_ptr<vcui::ScrollView> listScroll;
    std::shared_ptr<vcui::Stack> body;      // 列表 ↔ 详情
    std::shared_ptr<vcui::Stack> detail;    // 详情页（打开时重建内容）
    size_t detailGen = 0;                   // 丢弃过期的详情回调
    std::shared_ptr<vcui::Label> status;
    std::shared_ptr<vcui::Input> query;
    std::shared_ptr<vcui::Dropdown> order;
    std::shared_ptr<vcui::Label> pageInfo;
    std::shared_ptr<vcui::Widget> toolbar;   // 详情页时收起
    std::shared_ptr<vcui::Widget> foot;      // 详情页时收起（分页对详情没意义）
    std::shared_ptr<vcui::Stack> footHost;    // 卡片底部的分页区
    std::shared_ptr<vcui::Button> prev;
    std::shared_ptr<vcui::Button> next;

    // 筛选：模组加载器 / 游戏版本 / 光影加载器
    std::shared_ptr<vcui::Dropdown> fLoader;   // 模组、整合包：Fabric/Forge/NeoForge/Quilt
    std::shared_ptr<vcui::Dropdown> fShader;   // 光影包：Iris/OptiFine
    std::shared_ptr<vcui::Dropdown> fVer;      // 游戏版本（含自定义）
    std::shared_ptr<vcui::Input> verInput;     // 自定义版本输入
    std::shared_ptr<vcui::Widget> tip;         // 工具行右侧“源”标注；自定义版本时让位
    int loaderIdx = 0;
    int shaderIdx = 0;
    int verIdx = 0;
    std::wstring verCustom;

    // 游戏页：正式 / 快照 / 远古 三个频道，交给 VirtualList（自带滚动条，不分页）
    std::shared_ptr<vcui::VirtualList> gameList;
    std::shared_ptr<vcui::Segmented> channel;
    std::shared_ptr<std::vector<McVersion>> shown;
    std::shared_ptr<vcui::Stack> skel;        // 骨架行（清单没回来时占住列表位置）
    std::shared_ptr<vcui::Widget> skelWrap;   // 骨架区容器，和列表互斥显示
    std::shared_ptr<vcui::Spinner> spin;      // 转圈：工具行右侧
    int chan = 0;          // 0 正式 / 1 快照 / 2 远古

    std::vector<McVersion> versions;

    int sub = 0;
    std::wstring type;
    std::wstring index = L"relevance";
    std::wstring filter;

    int page = 0;          // 0-based
    int pages = 1;
    long long total = 0;
    size_t gen = 0;        // 丢弃过期回调
    bool bmclOk = true;    // 本机 BMCLAPI 是否可达
};

std::vector<std::wstring> DownloadPage::SubNav() {
    return { L"游戏", L"模组", L"整合包", L"资源包", L"光影包", L"数据包" };
}

DownloadPage::DownloadPage(UiCtx* ctx) : ctx_(ctx) {}

namespace {

int PageSizeFor(int sub) { return sub == 0 ? 30 : 20; }

// 隔行浅底（斑马纹）由设置决定，默认关；Build 时按当前设置刷新
bool gZebra = false;


std::shared_ptr<vcui::Stack> ItemRow(float gap = 10.f) {
    auto r = vcui::HBox(gap);
    r->Cross = vcui::Stack::Center;
    return r;
}

// 游戏页的行：整页只有一张卡片，行本身不再是卡片——隔行浅底 + 悬停套一圈细边框
class GameRow : public vcui::Widget {
public:
    bool Odd = false;
    std::shared_ptr<IconImageView> icon;
    std::shared_ptr<vcui::Badge> badge;
    std::shared_ptr<vcui::Label> name, date;
    std::shared_ptr<vcui::Button> btn;

    GameRow() { H = 44.f; Interactive = true; }
    D2D1_SIZE_F Desired(float availW, float) override { return D2D1::SizeF(availW, H); }
    void LayoutChildren() override {
        if (!kids_.empty()) kids_.front()->Arrange(Content());
    }
    vcui::CursorShape CursorAt(float, float) const override { return vcui::CursorShape::Hand; }
    void Paint(vcui::PaintCtx& ctx) override {
        if (gZebra && Odd) ctx.g.RoundRect(fr_, 2.f, vcui::Overlay(ctx.t, ctx.t.dark ? .055f : .03f));
        const float a = HoverA();
        if (a > .01f) {
            ctx.g.RoundRect(fr_, 2.f, vcui::Overlay(ctx.t, (ctx.t.dark ? .10f : .06f) * a));
            ctx.g.StrokeRect(fr_, 2.f, vcui::A(ctx.t.accent, .55f * a), 1.f);
        }
    }
};

// 模组/资源包等列表的行：同样是一张卡片里的裸行，隔行浅底 + 悬停细边框
class ModRow : public vcui::Widget {
public:
    bool Odd = false;
    ModRow() { H = 76.f; Interactive = true; }
    D2D1_SIZE_F Desired(float availW, float) override { return D2D1::SizeF(availW, H); }
    void LayoutChildren() override {
        if (!kids_.empty()) kids_.front()->Arrange(Content());
    }
    vcui::CursorShape CursorAt(float, float) const override { return vcui::CursorShape::Hand; }
    void Paint(vcui::PaintCtx& ctx) override {
        if (gZebra && Odd) ctx.g.RoundRect(fr_, 3.f, vcui::Overlay(ctx.t, ctx.t.dark ? .055f : .03f));
        const float a = HoverA();
        if (a > .01f) {
            ctx.g.RoundRect(fr_, 3.f, vcui::Overlay(ctx.t, (ctx.t.dark ? .10f : .06f) * a));
            ctx.g.StrokeRect(fr_, 3.f, vcui::A(ctx.t.accent, .55f * a), 1.f);
        }
    }
};

// 详情页「版本」页签的行：同样是卡片里的裸行，隔行浅底 + 悬停细边框
class VersionRow : public vcui::Widget {
public:
    bool Odd = false;
    std::shared_ptr<vcui::Label> name, meta, size;
    std::shared_ptr<vcui::Button> btn;

    VersionRow() { H = 56.f; }
    D2D1_SIZE_F Desired(float availW, float) override { return D2D1::SizeF(availW, H); }
    void LayoutChildren() override {
        if (!kids_.empty()) kids_.front()->Arrange(Content());
    }
    void Paint(vcui::PaintCtx& ctx) override {
        if (gZebra && Odd) ctx.g.RoundRect(fr_, 3.f, vcui::Overlay(ctx.t, ctx.t.dark ? .055f : .03f));
        const float a = HoverA();
        if (a > .01f)
            ctx.g.RoundRect(fr_, 3.f, vcui::Overlay(ctx.t, (ctx.t.dark ? .10f : .06f) * a));
    }
};

// 列表是异步回来才填的，构建期的 StaggerIn 覆盖不到，所以每次填完自己补一遍错峰入场
void RevealRows(const std::shared_ptr<vcui::Stack>& list, const Settings* st) {
    if (!list || (st && !st->animations)) return;
    float d = 0.f;
    for (auto& k : list->Children()) {
        k->FadeIn(.20f, 8.f, d);
        d = (std::min)(d + .025f, .16f);
    }
}

// 版本类型 → resource\image 里的方块图标（打包在 exe 里，走 res: 源）
std::wstring VersionIconPath(const std::wstring& type) {
    std::wstring name = L"Grass_Block.png";                  // release：草方块
    if (type == L"snapshot") name = L"Command_Block.png";     // snapshot：命令方块
    else if (type == L"old_beta" || type == L"old_alpha") name = L"StoneOldBeta.png";
    return L"res:image/" + name;
}

// 三级文字（更淡），和设置页一个口径
D2D1_COLOR_F MutedColor(const UiCtx* ctx) {
    return ctx && ctx->surface ? ctx->surface->Th().t.text3 : vcui::Hex(0x707070);
}

std::shared_ptr<vcui::Label> MutedCaption(const UiCtx* ctx, const std::wstring& s) {
    auto l = Caption(s);
    l->Color = MutedColor(ctx);
    return l;
}

// 汉化标题形如「实体渲染机制优化 (Entity Culling)」：结尾括号里的英文原名单独拆出来
void SplitTitle(const std::wstring& s, std::wstring& main, std::wstring* bracket) {
    main = s;
    if (!bracket) return;
    bracket->clear();
    size_t b = s.find_last_of(L"（(");
    if (b == std::wstring::npos || b == 0) return;
    size_t e = s.find_last_of(L"）)");
    if (e == std::wstring::npos || e != s.size() - 1 || e <= b + 1) return;
    std::wstring tail = s.substr(b);
    // 括号里得有点实货（防止「（已停更）」这种短标签被当成原名）
    if (tail.size() < 6) return;
    main = s.substr(0, b);
    while (!main.empty() && (main.back() == L' ' || main.back() == L'\t')) main.pop_back();
    if (main.empty()) { main = s; return; }
    *bracket = tail;
}

// 汉化名优先；没有就退回 Modrinth 的英文标题
std::wstring DisplayTitle(const UiCtx* ctx, const MrHit& h) {
    if (ctx && ctx->wiki) {
        if (const std::wstring* zh = ctx->wiki->Zh(h.slug)) {
            if (!zh->empty()) return *zh;
        }
    }
    return h.title.empty() ? h.slug : h.title;
}

// Modrinth 网页地址（类型决定路径段）
std::wstring ModrinthPageUrl(const MrHit& h) {
    std::wstring kind = L"mod";
    if (h.projectType == L"modpack") kind = L"modpack";
    else if (h.projectType == L"resourcepack") kind = L"resourcepack";
    else if (h.projectType == L"shader") kind = L"shader";
    else if (h.projectType == L"datapack") kind = L"datapack";
    const std::wstring id = h.slug.empty() ? h.id : h.slug;
    return L"https://modrinth.com/" + kind + L"/" + id;
}

std::wstring McmodPageUrl(int classId) {
    return L"https://www.mcmod.cn/class/" + std::to_wstring(classId) + L"/";
}

void OpenExternal(const std::wstring& url) {
    if (url.empty()) return;
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// 骨架块：自己画 N 行占位条，不做嵌套布局——避免行多了被父容器压到底边上叠成一坨
class SkelBlock : public vcui::Widget {
public:
    int Count = 9;
    float RowH = 44.f;
    float Gap = 0.f;

    SkelBlock() = default;
    SkelBlock(int n, float rowH) : Count(n), RowH(rowH) {}

    D2D1_SIZE_F Desired(float availW, float) override {
        return D2D1::SizeF(availW, float(Count) * RowH + Gap * float(Count > 0 ? Count - 1 : 0));
    }
    bool Update(float dt) override {
        vcui::Widget::Update(dt);
        t_ += dt;
        if (t_ > 3600.f) t_ = std::fmod(t_, 2.4f);
        AnimDirty = true;
        return true;                       // 呼吸要一直走帧
    }
    void Paint(vcui::PaintCtx& ctx) override {
        if (Count <= 0) return;
        D2D1_COLOR_F base = ctx.t.dark ? vcui::C(1, 1, 1, 1) : vcui::C(0, 0, 0, 1);
        float peak = ctx.t.dark ? .13f : .09f;
        float right = fr_.right - 10.f;
        for (int i = 0; i < Count; ++i) {
            float top = fr_.top + float(i) * (RowH + Gap);
            float k = .5f - .5f * std::cos((t_ + .12f * float(i)) / 2.4f * 6.2831853f);
            D2D1_COLOR_F c = base;
            c.a = peak * (.55f + .45f * k);
            float cy = top + RowH * .5f;
            ctx.g.RoundRect(D2D1::RectF(fr_.left + 10.f, cy - 14.f, fr_.left + 38.f, cy + 14.f), 5.f, c);
            ctx.g.RoundRect(D2D1::RectF(fr_.left + 48.f, cy - 9.f, fr_.left + 112.f, cy + 9.f), 9.f, c);
            float nameR = right - 70.f - 10.f - 78.f - 10.f;
            ctx.g.RoundRect(D2D1::RectF(fr_.left + 122.f, cy - 8.f, nameR, cy + 8.f), 4.f, c);
            ctx.g.RoundRect(D2D1::RectF(nameR + 10.f, cy - 7.f, nameR + 10.f + 78.f, cy + 7.f), 4.f, c);
            ctx.g.RoundRect(D2D1::RectF(right - 70.f, cy - 14.f, right, cy + 14.f), 4.f, c);
        }
    }
private:
    float t_ = 0.f;
};
// ---- 元数据小图标（SVG path，24×24 视口） ----
const wchar_t* kIcoDownload = L"M12 3.4v9.8M8.3 9.4L12 13.1 15.7 9.4M5 18.6h14";
const wchar_t* kIcoUser = L"M12 4C14.2 4 16 5.8 16 8C16 10.2 14.2 12 12 12C9.8 12 8 10.2 8 8C8 5.8 9.8 4 12 4Z";
const wchar_t* kIcoBody = L"M4 20.4C4 16.6 7.6 14.6 12 14.6C16.4 14.6 20 16.6 20 20.4";
const wchar_t* kIcoClock = L"M12 3C16.97 3 21 7.03 21 12C21 16.97 16.97 21 12 21C7.03 21 3 16.97 3 12C3 7.03 7.03 3 12 3Z";
const wchar_t* kIcoHands = L"M12 7.2v5.2l3.6 2.2";

// 公历日 → 自 1970-01-01 起的天数（Howard Hinnant 的算法，避免依赖时区）
long long DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

// "2026-11-25T10:00:00Z" → 今天 / 昨天 / 3天前 / 5个月前 / 2年前
std::wstring RelTime(const std::wstring& iso) {
    if (iso.size() < 10) return L"";
    int y = 0, m = 0, d = 0;
    if (swscanf_s(iso.c_str(), L"%d-%d-%d", &y, &m, &d) != 3) return L"";
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &now);
    long long days = DaysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) - DaysFromCivil(y, m, d);
    if (days < 0) days = 0;
    if (days == 0) return L"今天";
    if (days == 1) return L"昨天";
    if (days < 30) return WFormat(L"%lld天前", days);
    if (days < 365) return WFormat(L"%lld个月前", days / 30);
    return WFormat(L"%lld年前", days / 365);
}

// 一条元数据（图标 + 文字），整组定宽，卡片之间才能对齐
std::shared_ptr<vcui::Widget> MetaItem(const UiCtx* ctx, std::initializer_list<const wchar_t*> paths,
                                      const std::wstring& text, float groupW) {
    auto box = vcui::HBox(5.f);
    box->Cross = vcui::Stack::Center;
    box->W = groupW;
    // 多段 path 合成一个图标：各段共用同一个 24×24 视口，按整体 bbox 缩放
    std::wstring d;
    for (const wchar_t* path : paths) d += path;
    auto ic = std::make_shared<vcui::SvgIcon>();
    ic->Path = d;
    ic->IconSize = 13.f;
    ic->W = 13.f;
    ic->H = 13.f;
    ic->Stroke = 1.5f;
    box->Add(ic);
    auto lb = Caption(text);
    lb->Ellipsis = true;
    lb->Grow = 1.f;
    lb->Color = MutedColor(ctx);
    box->Add(lb);
    return box;
}
// 下载次数是"次数"不是字节：上万按万、上亿按亿，别走 HumanBytes
std::wstring TrimDec(std::wstring s, const std::wstring& unit) {
    size_t u = s.rfind(unit);
    if (u == std::wstring::npos) u = s.size();
    size_t dot = s.find(L'.');
    if (dot == std::wstring::npos) return s;
    size_t i = u;
    while (i > dot + 1 && s[i - 1] == L'0') --i;
    if (i == dot + 1) --i;      // 小数位全零：连小数点一起去掉
    s.erase(i, u - i);
    return s;
}

std::wstring CountText(long long n) {
    if (n < 0) n = 0;
    if (n < 10000) return WFormat(L"%lld", n);
    if (n < 100000000) {
        double v = (double)n / 10000.0;
        return v < 100.0 ? TrimDec(WFormat(L"%.1f万", v), L"万") : WFormat(L"%lld万", (long long)(v + .5));
    }
    double e = (double)n / 100000000.0;
    return e < 100.0 ? TrimDec(WFormat(L"%.2f亿", e), L"亿") : WFormat(L"%lld亿", (long long)(e + .5));
}

// 往容器里塞 n 条骨架行（错峰呼吸）
void FillSkeletons(vcui::Stack& box, int n, float gap, float rowH = 44.f) {
    box.Clear();
    box.Gap = gap;
    auto blk = std::make_shared<SkelBlock>(n, rowH);
    blk->Gap = gap;
    box.Add(blk);
}

// 筛选用的固定取值
const wchar_t* kLoaderSlug[5] = { L"", L"fabric", L"forge", L"neoforge", L"quilt" };
const wchar_t* kShaderSlug[3] = { L"", L"iris", L"optifine" };
// 版本下拉的预置项（索引 0 是"全部"，10 是"自定义"）
const wchar_t* kVerPreset[9] = { L"26.1.2", L"1.21.11", L"1.21.1", L"1.20.1", L"1.19.2",
                                 L"1.18.2", L"1.16.5", L"1.12.2", L"1.7.10" };

// Modrinth facets：组内 OR，组间 AND
std::vector<std::wstring> MrFacetGroups(const std::wstring& type, int loaderIdx, int shaderIdx,
                                        int verIdx, const std::wstring& verCustom) {
    std::vector<std::wstring> g;
    if (type == L"mod" || type == L"modpack") {
        if (loaderIdx > 0 && loaderIdx < 5) g.push_back(L"categories:" + std::wstring(kLoaderSlug[loaderIdx]));
    }
    if (type == L"shader") {
        if (shaderIdx > 0 && shaderIdx < 3) g.push_back(L"categories:" + std::wstring(kShaderSlug[shaderIdx]));
    }
    std::wstring ver;
    if (verIdx >= 1 && verIdx <= 9) ver = kVerPreset[verIdx - 1];
    else if (verIdx == 10) ver = verCustom;
    if (!ver.empty()) g.push_back(L"versions:" + ver);
    return g;
}

} // namespace

std::shared_ptr<vcui::Widget> DownloadPage::Build(int sub) {
    auto page = vcui::VBox(10.f);
    page->Grow = 1.f;

    gZebra = ctx_->settings && ctx_->settings->listZebra;

    auto s = std::make_shared<State>();
    s->sub = sub;

    // 状态行/分页必须先建好：BuildGame / BuildModrinth 里的加载回调会立刻写状态
    s->status = Caption(L"");
    s->status->Grow = 1.f;
    s->spin = std::make_shared<vcui::Spinner>(15.f);
    s->spin->Visible = false;
    s->prev = std::make_shared<vcui::Button>(L"上一页");
    s->prev->W = 88.f;
    s->pageInfo = std::make_shared<vcui::Label>(L"");
    s->pageInfo->UseType(vcui::Type::Caption1);
    s->pageInfo->W = 150.f;
    s->pageInfo->Align = vcui::HAlign::Center;
    s->next = std::make_shared<vcui::Button>(L"下一页");
    s->next->W = 88.f;

    if (sub == 0) {
        BuildGame(s, *page);
    } else {
        static const wchar_t* types[] = { L"", L"mod", L"modpack", L"resourcepack", L"shader", L"datapack" };
        s->type = types[sub < 6 ? sub : 1];
        BuildModrinth(s, *page);
    }

    auto self = this;
    // 游戏页改成了虚拟列表（自带滚动条、不分页），状态文案已经落在工具行里
    if (sub != 0) {
        auto foot = vcui::HBox(8.f);
        foot->W = -1.f;
        foot->Cross = vcui::Stack::Center;
        foot->PT = 8.f;
        foot->PB = 8.f;
        foot->PL = 16.f;
        foot->PR = 16.f;
        foot->Add(s->spin);
        foot->Add(s->status);
        foot->Add(s->prev);
        foot->Add(s->pageInfo);
        foot->Add(s->next);
        if (s->footHost) {
            s->footHost->Add(std::make_shared<vcui::Divider>());
            s->footHost->Add(foot);
        } else {
            page->Add(foot);
        }
        s->foot = foot;
        s->prev->Clicked = [self, s] { self->GoPage(s, -1); };
        s->next->Clicked = [self, s] { self->GoPage(s, +1); };
    }
    return page;
}

// ---------------- 游戏（BMCLAPI / Mojang） ----------------

void DownloadPage::BuildGame(const std::shared_ptr<State>& s, vcui::Stack& page) {
    // 整页一张卡片：工具栏、频道、列表、状态全在卡里，外面不再有散落的文字和按钮
    auto inner = vcui::VBox(0.f);
    inner->W = -1.f;

    auto bar = vcui::HBox(8.f);
    bar->W = -1.f;
    bar->Cross = vcui::Stack::Center;
    bar->PT = 12.f;
    bar->PB = 10.f;
    bar->PL = 16.f;
    bar->PR = 16.f;

    s->query = std::make_shared<vcui::Input>(L"过滤版本号，例如 1.20.1");
    s->query->W = 260.f;

    auto refresh = std::make_shared<vcui::Button>(L"刷新");

    auto sp = std::make_shared<vcui::Widget>();
    sp->Grow = 1.f;

    // 下载源统一在「设置 · 全局设置」里改，这一页只显示当前用的是哪个
    auto tip = Caption(ctx_->settings->mirror == L"mojang" ? L"源：Mojang 官方" : L"源：BMCLAPI");
    tip->Color = MutedColor(ctx_);

    bar->Add(s->query);
    bar->Add(refresh);
    bar->Add(sp);
    bar->Add(tip);
    inner->Add(bar);
    s->toolbar = bar;

    // 频道：正式 / 快照 / 远古。切换只重排列表，不重新请求
    auto chanRow = vcui::HBox(10.f);
    chanRow->W = -1.f;
    chanRow->Cross = vcui::Stack::Center;
    chanRow->PL = 16.f;
    chanRow->PR = 16.f;
    chanRow->PB = 10.f;
    s->channel = std::make_shared<vcui::Segmented>(
        std::vector<std::wstring>{ L"正式", L"快照", L"远古" }, 0);
    chanRow->Add(s->channel);
    chanRow->Add(s->spin);
    chanRow->Add(s->status);
    inner->Add(chanRow);

    inner->Add(std::make_shared<vcui::Divider>());

    // 骨架行：清单没回来时顶在列表位置，避免出现「空白一大片 + 一行字」
    auto skelWrap = vcui::VBox(0.f);
    skelWrap->Grow = 1.f;
    skelWrap->PL = 6.f;
    skelWrap->PR = 6.f;
    s->skel = vcui::VBox(0.f);
    s->skel->W = -1.f;
    FillSkeletons(*s->skel, 9, 0.f);
    skelWrap->Add(s->skel);
    skelWrap->Visible = false;
    inner->Add(skelWrap);
    s->skelWrap = skelWrap;

    // 虚拟列表：只建视口内那几行，滚动条由列表自己画
    auto listWrap = vcui::VBox(0.f);
    listWrap->Grow = 1.f;
    listWrap->PL = 6.f;
    listWrap->PR = 6.f;
    listWrap->PB = 6.f;
    s->gameList = std::make_shared<vcui::VirtualList>();
    s->gameList->RowHeight = 44.f;
    s->gameList->Gap = 0.f;
    s->gameList->ShowBar = true;
    s->gameList->BarW = 8.f;
    s->gameList->Grow = 1.f;
    listWrap->Add(s->gameList);
    inner->Add(listWrap);

    auto self = this;
    // 刷新：先把会话缓存清掉，再重新拉清单
    refresh->Clicked = [self, s] {
        if (self->ctx_->clearCache) self->ctx_->clearCache();
        s->page = 0;
        self->LoadManifest(s);
    };
    s->query->Changed = [self, s](const std::wstring& q) {
        s->filter = q;
        self->RenderVersions(s);
    };
    s->channel->Changed = [self, s](int idx) {
        s->chan = idx;
        self->RenderVersions(s);
    };

    auto card = std::make_shared<vcui::Card>();
    card->Shadow = false;
    card->Border = true;
    card->Radius = 4.f;
    card->Pad(0.f);
    card->Grow = 1.f;
    card->W = -1.f;
    card->Add(inner);
    page.Add(card);

    LoadManifest(s);
}
void DownloadPage::RenderVersions(const std::shared_ptr<State>& s) {
    std::wstring key = ToLower(Trim(s->filter));

    // 数据到位：收起骨架/转圈，把虚拟列表放回来
    if (s->skelWrap) s->skelWrap->Visible = false;
    if (s->gameList) s->gameList->Visible = true;
    if (s->spin) s->spin->Visible = false;

    auto pass = [](const McVersion& v, int chan) {
        if (chan == 0) return v.type == L"release";
        if (chan == 1) return v.type == L"snapshot";
        return v.type == L"old_beta" || v.type == L"old_alpha";
    };

    auto shown = std::make_shared<std::vector<McVersion>>();
    shown->reserve(s->versions.size());
    for (const auto& v : s->versions) {
        if (!pass(v, s->chan)) continue;
        if (!key.empty() && ToLower(v.id).find(key) == std::wstring::npos) continue;
        shown->push_back(v);
    }
    s->shown = shown;
    s->total = (long long)shown->size();
    s->page = 0;
    s->pages = 1;

    auto self = this;
    auto list = s->gameList;
    list->Reset();
    list->Build = [self, s](int idx) {
        auto r = std::make_shared<GameRow>();
        r->Odd = (idx % 2) == 1;

        auto row = vcui::HBox(10.f);
        row->W = -1.f;
        row->H = 44.f;
        row->Cross = vcui::Stack::Center;
        row->PL = 10.f;
        row->PR = 10.f;

        r->icon = std::make_shared<IconImageView>();
        r->icon->size = D2D1::SizeF(28.f, 28.f);
        r->icon->Radius = 4.f;
        row->Add(r->icon);

        r->badge = std::make_shared<vcui::Badge>(L"", vcui::Badge::Neutral);
        r->badge->W = 84.f;
        row->Add(r->badge);

        r->name = Text(L"", vcui::Type::Body1Strong);
        r->name->Grow = 1.f;
        r->name->Ellipsis = true;
        row->Add(r->name);

        r->date = Caption(L"");
        r->date->W = 86.f;
        row->Add(r->date);

        r->btn = std::make_shared<vcui::Button>(L"安装");
        r->btn->W = 70.f;
        r->btn->H = 30.f;
        row->Add(r->btn);

        r->Add(row);
        return r;
    };
    list->Bind = [self, s, shown](vcui::Widget& w, int i) {
        auto* r = dynamic_cast<GameRow*>(&w);
        if (!r || i < 0 || i >= (int)shown->size()) return false;
        const McVersion v = (*shown)[(size_t)i];
        r->icon->SetSource(self->ctx_->icons, VersionIconPath(v.type));
        r->badge->Text = v.type;
        r->badge->ToneKind = v.type == L"release" ? vcui::Badge::Accent : vcui::Badge::Neutral;
        r->name->Text = v.id;
        r->date->Text = IsoDateOnly(v.releaseTime);
        r->btn->Clicked = [self, s, v] { self->InstallGame(s, v); };
        return true;
    };
    list->ItemCount = (int)shown->size();
    if (ctx_->settings->animations) list->AnimateIn();

    if (s->status)
        s->status->Text = WFormat(L"%s%lld 个版本", key.empty() ? L"" : L"筛选结果 ", s->total);
}

void DownloadPage::LoadManifest(const std::shared_ptr<State>& s) {
    s->status->Text = L"正在获取版本清单…";
    if (s->spin) s->spin->Visible = true;
    if (s->skelWrap) s->skelWrap->Visible = true;
    if (s->gameList) s->gameList->Visible = false;
    // 自检钩子：VCL_UI_LOADING=1 只停在骨架态，不发请求，便于截屏核对加载动画
    if (_wgetenv(L"VCL_UI_LOADING")) return;
    bool bmcl = ctx_->settings->mirror != L"mojang";
    size_t gen = ++s->gen;
    auto self = this;
    // 镜像只留 8s 探测窗口：网络被拦截时能尽快回退官方
    ctx_->http->Get(McManifestUrl(bmcl), [self, s, bmcl, gen](bool ok, const std::string& body, const std::string& err) {
        if (s->gen != gen) return;
        if (!ok) {
            if (bmcl) {
                // BMCLAPI 不可达：这一次直接换官方源重试，设置里的下载源不动
                s->bmclOk = false;
                s->status->Text = L"BMCLAPI 不可达，已自动回退 Mojang 官方源…";
                size_t g2 = ++s->gen;
                self->ctx_->http->Get(McManifestUrl(false), [self, s, g2](bool ok2, const std::string& body2,
                                                                         const std::string& err2) {
                    if (s->gen != g2) return;
                    if (!ok2) {
                        s->status->Text = L"版本清单获取失败：" + Utf8ToWide(err2);
                        if (s->spin) s->spin->Visible = false;
                        if (s->skelWrap) s->skelWrap->Visible = false;
                        if (s->gameList) s->gameList->Visible = true;
                        return;
                    }
                    std::string pe;
                    if (!ParseMcManifest(body2, s->versions, &pe)) {
                        s->status->Text = L"清单解析失败：" + Utf8ToWide(pe);
                        if (s->spin) s->spin->Visible = false;
                        if (s->skelWrap) s->skelWrap->Visible = false;
                        if (s->gameList) s->gameList->Visible = true;
                        return;
                    }
                    self->RenderVersions(s);
                    if (s->status)
                        s->status->Text = WFormat(L"%lld 个版本（Mojang）", s->total);
                }, {}, 25000);
                return;
            }
            s->status->Text = L"版本清单获取失败：" + Utf8ToWide(err);
            if (s->spin) s->spin->Visible = false;
            if (s->skelWrap) s->skelWrap->Visible = false;
            if (s->gameList) s->gameList->Visible = true;
            return;
        }
        s->bmclOk = bmcl;
        std::string pe;
        if (!ParseMcManifest(body, s->versions, &pe)) {
            s->status->Text = L"清单解析失败：" + Utf8ToWide(pe);
            if (s->spin) s->spin->Visible = false;
            if (s->skelWrap) s->skelWrap->Visible = false;
            if (s->gameList) s->gameList->Visible = true;
            return;
        }
        if (bmcl)
            for (auto& v : s->versions) v.url = MirrorUrl(v.url, true);
        self->RenderVersions(s);
        if (s->status)
            s->status->Text = WFormat(L"%lld 个版本（%s）", s->total, bmcl ? L"BMCLAPI" : L"Mojang");
    }, {}, bmcl ? 8000 : 25000);
}

void DownloadPage::InstallGame(const std::shared_ptr<State>& s, const McVersion& v) {
    s->status->Text = L"正在读取 " + v.id + L" 的版本元数据…";
    auto ver = v;
    auto self = this;
    ctx_->http->Get(v.url, [self, s, ver](bool ok, const std::string& body, const std::string& err) {
        if (!ok) {
            s->status->Text = L"版本元数据获取失败：" + Utf8ToWide(err);
            return;
        }
        McClientFile f;
        std::string pe;
        if (!ParseMcVersionJson(body, f, &pe)) {
            s->status->Text = L"版本元数据不完整：" + Utf8ToWide(pe);
            return;
        }

        std::wstring root = CurrentRootPath(*self->ctx_->settings);
        std::wstring dir = JoinPath(JoinPath(root, L"versions"), ver.id);
        EnsureDir(dir);

        std::wstring jsonPath = JoinPath(dir, ver.id + L".json");
        {
            std::ofstream out(jsonPath, std::ios::binary | std::ios::trunc);
            if (out) out.write(body.data(), (std::streamsize)body.size());
        }

        std::wstring jarPath = JoinPath(dir, ver.id + L".jar");
        std::wstring official = f.url;
        std::wstring mirrored = MirrorUrl(f.url, true);
        std::wstring primary = s->bmclOk ? mirrored : official;
        std::wstring fallback = s->bmclOk ? official : mirrored;
        std::vector<std::wstring> mirrors;
        if (fallback != primary) mirrors.push_back(fallback);

        if (self->ctx_->dl->Enqueue(primary, jarPath, f.sha1, f.size, mirrors)) {
            s->status->Text = WFormat(L"已加入队列：%s · client.jar %s", ver.id.c_str(),
                                      HumanBytes(f.size).c_str());
            self->ctx_->Toast(L"开始下载 " + ver.id, vcui::InfoTone::Success);
        } else {
            s->status->Text = L"入队失败（目标文件可能正被占用）";
        }
    });
}

// ---------------- Modrinth ----------------

void DownloadPage::BuildModrinth(const std::shared_ptr<State>& s, vcui::Stack& page) {
    s->query = std::make_shared<vcui::Input>(L"搜索" + MrTypeName(s->type));
    s->query->W = 232.f;

    s->order = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"相关度", L"下载量", L"关注数" }, 0);
    s->order->W = 112.f;

    // 模组 / 整合包：模组加载器
    if (s->type == L"mod" || s->type == L"modpack") {
        s->fLoader = std::make_shared<vcui::Dropdown>(
            std::vector<std::wstring>{ L"全部加载器", L"Fabric", L"Forge", L"NeoForge", L"Quilt" }, 0);
        s->fLoader->W = 142.f;
    }
    // 光影包：光影加载器
    if (s->type == L"shader") {
        s->fShader = std::make_shared<vcui::Dropdown>(
            std::vector<std::wstring>{ L"全部加载器", L"Iris", L"OptiFine" }, 0);
        s->fShader->W = 140.f;
    }
    // 全部类型都有游戏版本筛选
    {
        std::vector<std::wstring> items{ L"全部版本" };
        for (int i = 0; i < 9; ++i) items.push_back(kVerPreset[i]);
        items.push_back(L"自定义");
        s->fVer = std::make_shared<vcui::Dropdown>(items, 0);
        s->fVer->W = 122.f;
    }
    s->verInput = std::make_shared<vcui::Input>(L"如 1.21.4");
    s->verInput->W = 104.f;
    s->verInput->Visible = false;

    auto go = std::make_shared<vcui::Button>(L"搜索", true);
    auto tip = Caption(L"源：Modrinth");
    tip->Color = MutedColor(ctx_);
    s->tip = tip;
    auto sp = vcui::Spacer(0.f);
    sp->Grow = 1.f;

    auto inner = vcui::VBox(0.f);
    inner->W = -1.f;

    auto bar = vcui::HBox(8.f);
    bar->W = -1.f;
    bar->Cross = vcui::Stack::Center;
    bar->PT = 12.f;
    bar->PB = 10.f;
    bar->PL = 16.f;
    bar->PR = 16.f;
    bar->Add(s->query);
    bar->Add(s->order);
    if (s->fLoader) bar->Add(s->fLoader);
    if (s->fShader) bar->Add(s->fShader);
    bar->Add(s->fVer);
    bar->Add(s->verInput);
    bar->Add(go);
    bar->Add(sp);
    bar->Add(tip);
    inner->Add(bar);
    s->toolbar = bar;

    inner->Add(std::make_shared<vcui::Divider>());

    s->list = vcui::VBox(0.f);
    s->list->W = -1.f;
    s->listScroll = std::make_shared<vcui::ScrollView>();
    s->listScroll->Grow = 1.f;
    s->listScroll->ShowBar = true;
    s->listScroll->Add(s->list);

    // 列表和详情共用同一块内容区：点行进详情，返回时列表原样还在
    s->body = vcui::VBox(0.f);
    s->body->Grow = 1.f;
    s->body->PL = 6.f;
    s->body->PR = 6.f;
    s->body->PB = 6.f;
    s->body->Add(s->listScroll);
    inner->Add(s->body);

    s->footHost = vcui::VBox(0.f);
    s->footHost->W = -1.f;
    inner->Add(s->footHost);

    auto card = std::make_shared<vcui::Card>();
    card->Shadow = false;
    card->Border = true;
    card->Radius = 4.f;
    card->Pad(0.f);
    card->Grow = 1.f;
    card->W = -1.f;
    card->Add(inner);
    page.Add(card);

    auto self = this;
    s->order->Changed = [self, s](int idx) {
        s->index = idx == 0 ? L"relevance" : (idx == 1 ? L"downloads" : L"follows");
        if (self->ctx_->clearCache) self->ctx_->clearCache();   // 换排序 = 换结果集，旧缓存没用
        self->LoadSearch(s, 0);
    };

    // 筛选变了就重查：换的是结果集，清掉旧的查询缓存
    auto refilter = [self, s] {
        if (self->ctx_->clearCache) self->ctx_->clearCache();
        self->LoadSearch(s, 0);
    };
    if (s->fLoader) {
        s->fLoader->Changed = [s, refilter](int idx) { s->loaderIdx = idx; refilter(); };
    }
    if (s->fShader) {
        s->fShader->Changed = [s, refilter](int idx) { s->shaderIdx = idx; refilter(); };
    }
    s->fVer->Changed = [self, s, refilter](int idx) {
        s->verIdx = idx;
        // 选"自定义"才亮出输入框；切回预置项就把输入框收起
        bool custom = (idx == 10);
        if (s->verInput) s->verInput->Visible = custom;
        // 自定义输入框占位后工具行会挤：收起右侧“源”标注，搜索框临时收窄
        if (s->tip) s->tip->Visible = !custom;
        if (s->query) s->query->W = custom ? 168.f : 232.f;
        if (self->ctx_->surface) self->ctx_->surface->Invalidate();
        if (!custom) refilter();
    };
    s->verInput->Submitted = [self, s, refilter] {
        if (!s->verInput) return;
        std::wstring v = Trim(s->verInput->Text);
        if (v.empty()) {
            s->status->Text = L"先填一个版本号，例如 1.21.4";
            if (self->ctx_->surface) self->ctx_->surface->Invalidate();
            return;
        }
        s->verCustom = v;
        s->verIdx = 10;
        s->verInput->Visible = true;
        refilter();
    };

    go->Clicked = [self, s] {
        if (self->ctx_->clearCache) self->ctx_->clearCache();
        self->LoadSearch(s, 0);
    };
    s->query->Submitted = [self, s] {
        if (self->ctx_->clearCache) self->ctx_->clearCache();
        self->LoadSearch(s, 0);
    };

    // 自检钩子：VCL_UI_TEST=mods + VCL_UI_QUERY=更多箱子 直接落到"中文检索"的结果上
    if (const wchar_t* q = _wgetenv(L"VCL_UI_QUERY")) {
        if (*q) s->query->Text = q;
    }
    LoadSearch(s, 0);
}

void DownloadPage::LoadSearch(const std::shared_ptr<State>& s, int pageIdx) {
    int size = PageSizeFor(s->sub);
    s->status->Text = L"正在查询 Modrinth…";
    if (s->spin) s->spin->Visible = true;
    if (s->list) FillSkeletons(*s->list, 6, 0.f, 76.f);   // 先铺骨架，结果回来整块换掉
    if (_wgetenv(L"VCL_UI_LOADING")) return;        // 自检钩子：停在骨架态
    size_t gen = ++s->gen;
    auto self = this;

    // 中文查询：先在汉化表里命中 slug，再拿条目自带的英文名去查 Modrinth，
    // 返回后用命中的 slug 集合过滤（表里没有中文名时才退回英文名结果）
    std::wstring typed = Trim(s->query ? s->query->Text : L"");
    std::wstring remoteQ = typed;
    std::vector<std::wstring> zhSlugs;
    bool zhSearch = ModWiki::HasCjk(typed) && ctx_->wiki && ctx_->wiki->Loaded();
    if (zhSearch) {
        std::wstring en;
        ctx_->wiki->ResolveZh(typed, zhSlugs, en);
        if (!en.empty()) remoteQ = en;
        else if (!zhSlugs.empty()) remoteQ = zhSlugs.front();   // 没括号就用 slug 试一把
        if (zhSlugs.empty()) {
            s->status->Text = L"汉化表里没有这个词，试试英文名或缩写";
            s->list->Clear();
            if (s->spin) s->spin->Visible = false;
            s->total = 0;
            s->pages = 1;
            s->page = 0;
            UpdatePager(s);
            return;
        }
    }

    // 中文检索是一次抓齐再本地筛选：一次多要点，让汉化命中的项目尽量落在同一页
    int fetchSize = zhSearch ? (std::min)(100, (int)zhSlugs.size() * 3 + 10) : size;

    ctx_->http->Get(MrSearchUrl(s->type, remoteQ, s->index, fetchSize, zhSearch ? 0 : pageIdx * size,
                                MrFacetGroups(s->type, s->loaderIdx, s->shaderIdx, s->verIdx, s->verCustom)),
                    [self, s, gen, pageIdx, size, zhSearch, zhs = std::move(zhSlugs)](bool ok, const std::string& body,
                                                                                    const std::string& err) {
                        if (s->gen != gen) return;
                        if (!ok) {
                            s->status->Text = L"Modrinth 查询失败：" + Utf8ToWide(err);
                            s->list->Clear();
                            if (s->spin) s->spin->Visible = false;
                            return;
                        }
                        std::vector<MrHit> hits;
                        long long total = 0;
                        std::string pe;
                        if (!ParseMrSearch(body, hits, &pe, &total)) {
                            s->status->Text = L"响应解析失败：" + Utf8ToWide(pe);
                            s->list->Clear();
                            if (s->spin) s->spin->Visible = false;
                            return;
                        }
                        if (s->spin) s->spin->Visible = false;
                        if (!zhs.empty()) {
                            // 中文检索：只留下汉化表里命中的项目；一个都没命中就当英文检索的结果
                            std::vector<MrHit> keep;
                            for (const auto& h : hits)
                                if (std::find(zhs.begin(), zhs.end(), ToLower(h.slug)) != zhs.end())
                                    keep.push_back(h);
                            if (!keep.empty()) { hits.swap(keep); total = (long long)hits.size(); }
                        }
                        s->page = zhSearch ? 0 : pageIdx;
                        s->total = total;
                        // 中文检索是一次抓齐再本地筛选，远端翻页会跟筛选集对不上，所以只留一页
                        s->pages = zhSearch ? 1 : (total > 0 ? (int)((total + size - 1) / size) : 1);
                        if (s->page >= s->pages) s->page = s->pages - 1;

                        s->list->Clear();
                        for (const auto& h : hits) {
                            auto card = std::make_shared<ModRow>();
                            card->Odd = (s->list->Children().size() % 2) == 1;
                            auto row = ItemRow(12.f);
                            row->W = -1.f;
                            row->H = 76.f;
                            row->PT = 10.f;
                            row->PB = 10.f;
                            row->PL = 12.f;
                            row->PR = 12.f;

                            auto icon = std::make_shared<IconImageView>();
                            icon->SetSource(self->ctx_->icons, h.iconUrl);
                            icon->Tooltip = h.title.empty() ? h.slug : h.title;
                            row->Add(icon);

                            auto col = vcui::VBox(2.f);
                            col->Grow = 1.f;
                            std::wstring tMain, tBracket;
                            SplitTitle(DisplayTitle(self->ctx_, h), tMain, &tBracket);
                            if (tBracket.empty()) {
                                auto title = Text(tMain, vcui::Type::Body1Strong);
                                title->Ellipsis = true;
                                col->Add(title);
                            } else {
                                auto titleRow = vcui::HBox(5.f);
                                titleRow->Cross = vcui::Stack::End;
                                auto title = Text(tMain, vcui::Type::Body1Strong);
                                titleRow->Add(title);
                                auto sub = MutedCaption(self->ctx_, tBracket);
                                sub->Color = MutedColor(self->ctx_);
                                titleRow->Add(sub);
                                col->Add(titleRow);
                            }
                            auto desc = MutedCaption(self->ctx_, h.description);
                            desc->Ellipsis = true;
                            col->Add(desc);
                            auto meta = vcui::HBox(14.f);
                            meta->Cross = vcui::Stack::Center;
                            meta->Add(MetaItem(self->ctx_, {kIcoDownload}, CountText(h.downloads), 84.f));
                            meta->Add(MetaItem(self->ctx_, {kIcoUser, kIcoBody}, h.author, 150.f));
                            meta->Add(MetaItem(self->ctx_, {kIcoClock, kIcoHands}, RelTime(h.updated), 92.f));
                            col->Add(meta);
                            row->Add(col);

                            auto hit = h;
                            card->Clicked = [self, s, hit] { self->ShowDetail(s, hit); };

                            card->Add(row);
                            s->list->Add(card);
                        }
                        if (hits.empty()) s->list->Add(Caption(L"没有找到匹配的项目"));
                        RevealRows(s->list, self->ctx_->settings);

                        if (!zhs.empty() && !hits.empty())
                            s->status->Text = WFormat(L"共 %lld 条结果 · 中文检索命中", s->total);
                        else
                            s->status->Text = WFormat(L"共 %lld 条结果", s->total);
                        self->UpdatePager(s);
                    });
}

// ---------------- 分页 ----------------

void DownloadPage::UpdatePager(const std::shared_ptr<State>& s) {
    if (!s->pageInfo || !s->prev || !s->next) return;
    s->pageInfo->Text = WFormat(L"第 %d / %d 页", s->page + 1, s->pages > 0 ? s->pages : 1);
    s->prev->Enabled = s->page > 0;
    s->next->Enabled = s->page + 1 < s->pages;
}

void DownloadPage::GoPage(const std::shared_ptr<State>& s, int delta) {
    int target = s->page + delta;
    if (target < 0 || target >= s->pages) return;
    if (s->sub == 0) {
        s->page = target;
        RenderVersions(s);
    } else {
        LoadSearch(s, target);
    }
}

// ---------------- 详情页（版本 / 介绍） ----------------

void DownloadPage::ShowDetail(const std::shared_ptr<State>& s, const MrHit& hit) {
    if (!s->body) return;

    if (!s->detail) {
        s->detail = vcui::VBox(12.f);
        s->detail->Grow = 1.f;
        s->detail->PT = 12.f;
        s->detail->PL = 16.f;
        s->detail->PR = 16.f;
        s->detail->PB = 12.f;
        s->detail->Visible = false;
        s->body->Add(s->detail);
    }
    s->detail->Clear();
    s->detail->Visible = true;
    if (s->listScroll) s->listScroll->Visible = false;
    if (s->toolbar) s->toolbar->Visible = false;
    if (s->foot) s->foot->Visible = false;
    size_t gen = ++s->detailGen;

    auto self = this;

    // 顶部：返回 + 图标 + 标题/元信息
    auto head = vcui::HBox(12.f);
    head->Cross = vcui::Stack::Center;

    auto back = std::make_shared<vcui::IconButton>(L"\xE72B");
    back->Tooltip = L"返回列表";
    back->Clicked = [self, s] {
        ++s->detailGen;                     // 丢掉在途回调
        if (s->detail) s->detail->Visible = false;
        if (s->listScroll) s->listScroll->Visible = true;
        if (s->toolbar) s->toolbar->Visible = true;
        if (s->foot) s->foot->Visible = true;
        if (self->ctx_->surface) {
            self->ctx_->surface->Invalidate();
            self->ctx_->surface->PlayAnim();
        }
    };
    head->Add(back);

    auto icon = std::make_shared<IconImageView>();
    icon->size = D2D1::SizeF(44.f, 44.f);
    icon->Radius = 6.f;
    icon->SetSource(ctx_->icons, hit.iconUrl);
    head->Add(icon);

    auto col = vcui::VBox(2.f);
    col->Grow = 1.f;
    std::wstring tMain, tBracket;
    SplitTitle(DisplayTitle(ctx_, hit), tMain, &tBracket);
    if (tBracket.empty()) {
        auto title = std::make_shared<vcui::Label>(tMain, 20.f, 400);
        title->Ellipsis = true;
        col->Add(title);
    } else {
        auto titleRow = vcui::HBox(6.f);
        titleRow->Cross = vcui::Stack::End;
        auto title = std::make_shared<vcui::Label>(tMain, 20.f, 400);
        titleRow->Add(title);
        titleRow->Add(MutedCaption(ctx_, tBracket));
        col->Add(titleRow);
    }

    std::wstring sub = L"@";
    sub += hit.author.empty() ? std::wstring(L"unknown") : hit.author;
    if (!hit.title.empty()) sub += L" · " + hit.title;
    sub += L" · " + MrTypeName(hit.projectType);
    sub += L" · " + CountText(hit.downloads) + L" 次下载";
    std::wstring rel = RelTime(hit.updated);
    if (!rel.empty()) sub += L" · 更新于 " + rel;
    auto meta = Caption(sub);
    meta->Ellipsis = true;
    col->Add(meta);
    head->Add(col);

    // 外链：Modrinth 一定有；汉化表里带 mcmod 编号的再加一个 MC百科
    auto links = vcui::HBox(8.f);
    links->Cross = vcui::Stack::Center;

    auto toModrinth = std::make_shared<vcui::Button>(L"转到 Modrinth");
    toModrinth->Glyph = L"\xE8A7";   // OpenInNewWindow
    toModrinth->Clicked = [hit] { OpenExternal(ModrinthPageUrl(hit)); };
    links->Add(toModrinth);

    const int mcmod = ctx_->wiki ? ctx_->wiki->Mcmod(hit.slug) : 0;
    if (mcmod > 0) {
        auto toMcmod = std::make_shared<vcui::Button>(L"转到 MC百科");
        toMcmod->Glyph = L"\xE8A7";
        toMcmod->Tooltip = L"mcmod.cn/class/" + std::to_wstring(mcmod);
        toMcmod->Clicked = [mcmod] { OpenExternal(McmodPageUrl(mcmod)); };
        links->Add(toMcmod);
    }
    head->Add(links);

    s->detail->Add(head);
    s->detail->Add(std::make_shared<vcui::Divider>());

    auto tabs = std::make_shared<vcui::Segmented>(std::vector<std::wstring>{ L"版本", L"介绍" }, 0);
    // Segmented 在 Stretch 的 VBox 里会被拉满整行，套一层 Start 的 HBox 保持自身宽度
    auto tabRow = vcui::HBox(0.f);
    tabRow->Cross = vcui::Stack::Start;
    tabRow->Add(tabs);
    s->detail->Add(tabRow);

    // 两个页签的内容叠在同一块区域里，用 Visible 切换
    auto pages = vcui::VBox(0.f);
    pages->Grow = 1.f;

    auto vStatus = Caption(L"正在获取版本列表…");
    auto vList = std::make_shared<vcui::VirtualList>();
    vList->RowHeight = 62.f;
    vList->Gap = 6.f;
    vList->Grow = 1.f;
    auto vSpin = std::make_shared<vcui::Spinner>(14.f);
    auto vHead = vcui::HBox(8.f);
    vHead->Cross = vcui::Stack::Center;
    vHead->Add(vSpin);
    vHead->Add(vStatus);
    auto vPage = vcui::VBox(8.f);
    vPage->Grow = 1.f;
    vPage->Add(vHead);
    vPage->Add(vList);

    auto iStatus = Caption(L"正在获取项目介绍…");
    auto iBox = vcui::VBox(0.f);
    iBox->Grow = 1.f;
    auto iScroll = std::make_shared<vcui::ScrollView>();
    iScroll->Grow = 1.f;
    iScroll->Add(iBox);
    auto iSpin = std::make_shared<vcui::Spinner>(14.f);
    auto iHead = vcui::HBox(8.f);
    iHead->Cross = vcui::Stack::Center;
    iHead->Add(iSpin);
    iHead->Add(iStatus);
    auto iPage = vcui::VBox(8.f);
    iPage->Grow = 1.f;
    iPage->Visible = false;
    iPage->Add(iHead);
    iPage->Add(iScroll);

    pages->Add(vPage);
    pages->Add(iPage);
    s->detail->Add(pages);

    tabs->Changed = [vPage, iPage](int idx) {
        vPage->Visible = idx == 0;
        iPage->Visible = idx == 1;
    };

    LoadDetailVersions(s, hit, vStatus, vList, gen, vSpin);
    LoadDetailIntro(s, hit, iStatus, iBox, gen, iSpin);

    if (ctx_->surface) {
        ctx_->surface->Invalidate();
        ctx_->surface->PlayAnim();
    }
}

void DownloadPage::LoadDetailVersions(const std::shared_ptr<State>& s, const MrHit& hit,
                                      const std::shared_ptr<vcui::Label>& status,
                                      const std::shared_ptr<vcui::VirtualList>& list, size_t gen,
                                      const std::shared_ptr<vcui::Spinner>& spin) {
    auto self = this;
    auto selfType = s->type;
    ctx_->http->Get(MrVersionsUrl(hit.id),
                    [self, s, gen, status, list, selfType, spin](bool ok, const std::string& body,
                                                                const std::string& err) {
                        if (s->detailGen != gen) return;
                        spin->Visible = false;
                        if (!ok) {
                            status->Text = L"获取失败：" + Utf8ToWide(err);
                            return;
                        }
                        std::vector<MrFile> files;
                        std::string pe;
                        if (!ParseMrVersions(body, files, &pe)) {
                            status->Text = L"解析失败：" + Utf8ToWide(pe);
                            return;
                        }
                        status->Text = WFormat(L"%zu 个文件（按发布时间倒序）", files.size());

                        // 版本列表动辄上千条：交给 VirtualList，只建视口内那几行，滚走复用
                        auto filesPtr = std::make_shared<std::vector<MrFile>>(std::move(files));
                        list->Reset();
                        list->Build = [filesPtr](int idx) {
                            auto r = std::make_shared<VersionRow>();
                            r->Odd = (idx % 2) == 1;
                            auto row = vcui::HBox(10.f);
                            row->W = -1.f;
                            row->H = 56.f;
                            row->Cross = vcui::Stack::Center;
                            row->PT = 8.f;
                            row->PB = 8.f;
                            row->PL = 12.f;
                            row->PR = 12.f;
                            auto col = vcui::VBox(2.f);
                            col->Grow = 1.f;
                            r->name = Text(L"", vcui::Type::Body1Strong);
                            r->name->Ellipsis = true;
                            col->Add(r->name);
                            r->meta = Caption(L"");
                            r->meta->Ellipsis = true;
                            col->Add(r->meta);
                            row->Add(col);
                            r->size = Caption(L"");
                            row->Add(r->size);
                            r->btn = std::make_shared<vcui::Button>(L"下载");
                            r->btn->W = 76.f;
                            row->Add(r->btn);
                            r->Add(row);
                            return r;
                        };
                        list->Bind = [filesPtr, self, selfType](vcui::Widget& w, int i) {
                            auto* r = dynamic_cast<VersionRow*>(&w);
                            if (!r || i < 0 || i >= (int)filesPtr->size()) return false;
                            const MrFile& f = (*filesPtr)[(size_t)i];
                            r->name->Text = f.name;
                            r->meta->Text = WFormat(L"%s · %s · %s", f.versionName.c_str(),
                                                    f.loaders.c_str(), f.gameVersions.c_str());
                            r->size->Text = HumanBytes(f.size);
                            auto file = f;
                            r->btn->Clicked = [self, selfType, file] {
                                std::wstring root = CurrentRootPath(*self->ctx_->settings);
                                std::wstring path = JoinPath(JoinPath(root, MrTargetFolder(selfType)), file.name);
                                if (self->ctx_->dl->Enqueue(file.url, path, file.sha1, file.size))
                                    self->ctx_->Toast(L"已加入下载：" + file.name, vcui::InfoTone::Success);
                                else
                                    self->ctx_->Toast(L"入队失败：" + file.name, vcui::InfoTone::Danger);
                            };
                            return true;
                        };
                        list->ItemCount = (int)filesPtr->size();
                        if (self->ctx_->settings->animations) list->AnimateIn();
                    });
}
void DownloadPage::LoadDetailIntro(const std::shared_ptr<State>& s, const MrHit& hit,
                                   const std::shared_ptr<vcui::Label>& status,
                                   const std::shared_ptr<vcui::Stack>& holder, size_t gen,
                                   const std::shared_ptr<vcui::Spinner>& spin) {
    auto self = this;
    auto fallback = hit.description;
    ctx_->http->Get(MrProjectUrl(hit.id),
                    [self, s, gen, status, holder, fallback, spin](bool ok, const std::string& body,
                                                                  const std::string& err) {
                        if (s->detailGen != gen) return;
                        spin->Visible = false;
                        if (!ok) {
                            status->Text = L"介绍获取失败：" + Utf8ToWide(err);
                            return;
                        }
                        MrProject p;
                        std::string pe;
                        if (!ParseMrProject(body, p, &pe)) {
                            status->Text = L"介绍解析失败：" + Utf8ToWide(pe);
                            return;
                        }
                        std::wstring md = p.bodyMd.empty() ? fallback : p.bodyMd;
                        auto blocks = ParseMarkdown(md);
                        holder->Clear();
                        if (blocks.empty()) {
                            holder->Add(Caption(L"这个项目没有填写 Markdown 介绍。"));
                        } else if (self->ctx_->surface) {
                            holder->Add(BuildMarkdown(blocks, self->ctx_->surface->Th().t));
                        }
                        std::wstring info = WFormat(L"%zu 段介绍", blocks.size());
                        if (!p.license.empty()) info += L" · 许可证 " + p.license;
                        if (!p.categories.empty()) {
                            info += L" · ";
                            for (size_t i = 0; i < p.categories.size(); ++i) {
                                if (i) info += L" / ";
                                info += p.categories[i];
                            }
                        }
                        status->Text = info;
                    });
}

} // namespace vcl
