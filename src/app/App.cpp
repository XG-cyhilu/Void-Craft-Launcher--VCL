#include "app/App.h"
#include "ui/DownloadPage.h"
#include "ui/AboutPage.h"
#include "ui/SettingsPage.h"
#include "ui/JavaPage.h"
#include "ui/AccountPage.h"
#include "core/IconStore.h"
#include "core/ModWiki.h"
#include "core/Paths.h"
#include "core/Text.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>

namespace vcl {

App::App() {
    LoadSettings(settings_);
    // 会话缓存：启动先扫一遍，防止上次异常退出留下旧文件
    ClearTree(HttpCacheDir());
    ClearTree(IconCacheDir());
    EnsureDir(HttpCacheDir());
    EnsureDir(IconCacheDir());
    http_.SetCacheDir(HttpCacheDir());
    dl_ = std::make_unique<Downloader>(settings_.concurrency > 0 ? settings_.concurrency : 16,
                                       settings_.fileThreads > 0 ? settings_.fileThreads : 8,
                                       (long long)settings_.chunkThresholdKb * 1024);
    ctx_.http = &http_;
    ctx_.dl = dl_.get();
    ctx_.settings = &settings_;
    ctx_.saveSettings = [this] { SaveSettings(); };
    ctx_.applyUi = [this] { ApplyUi(); };
    ctx_.clearCache = [this] { ClearNetCache(); };
    // 汉化表：打包在 exe 里的 resource/modwiki/ModWiki.json；开发期可退回目录上的文件
    wiki_ = std::make_unique<ModWiki>();
    if (!wiki_->LoadEmbedded())
        wiki_->Load(JoinPath(JoinPath(ResourceDir(), L"modwiki"), L"ModWiki.json"));
    ctx_.wiki = wiki_.get();
    download_ = std::make_unique<DownloadPage>(&ctx_);
    settingsPage_ = std::make_unique<SettingsPage>(&ctx_);
    aboutPage_ = std::make_unique<AboutPage>(&ctx_);
    javaPage_ = std::make_unique<JavaPage>(&ctx_);
    accountPage_ = std::make_unique<AccountPage>(&ctx_);
}

App::~App() = default;

int App::Run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    vcui::AppOptions opt;
    opt.Title = L"Void Craft Launcher";
    opt.Width = 1180;
    opt.Height = 780;
    opt.MinWidth = 940;
    opt.MinHeight = 600;
    opt.TitleBar = true;
    opt.Build = [this](vcui::Widget& body) { Build(body); };
    ownedSurface_ = vcui::CreateAppSurface(opt);
    if (!ownedSurface_) {
        CoUninitialize();
        return 1;
    }
    surface_ = ownedSurface_.get();
    ctx_.surface = surface_;
    icons_ = std::make_unique<IconStore>(surface_);
    ctx_.icons = icons_.get();
    // 网络回调落在 worker 线程：直接把它塞进结果队列后叫醒帧循环，否则休眠时没人 Pump
    auto wake = [this] {
        if (!surface_) return;
        surface_->KickAnim();      // 让 Tick 继续跑
        surface_->Invalidate();    // InvalidateRect 可跨线程调用，顺带把消息循环唤醒
    };
    http_.onWake = wake;
    icons_->SetWake(wake);
    InitTitleBar();
    ApplyUi();

    // 自检钩子：VCL_UI_TEST=settings|global|mods|panel 直接落到某一页，便于截屏核对
    if (const wchar_t* m = _wgetenv(L"VCL_UI_TEST")) {
        std::wstring mode = m;
        if (mode == L"home") ShowPage(0, 0);
        else if (mode == L"settings") ShowPage(4, 0);
        else if (mode == L"global") ShowPage(4, 1);
        else if (mode == L"dl") {
            std::wstring out = JoinPath(std::wstring(_wgetenv(L"TEMP")), L"vcl-dl-test.json");
            bool ok = dl_->Enqueue(L"https://api.modrinth.com/v2/project/sodium", out, L"", 0);
        }
        else if (mode == L"versions") ShowPage(1, 0);
        else if (mode == L"manage") ShowPage(3, 0);
        else if (mode == L"java") ShowPage(3, 0);
        else if (mode == L"account") ShowPage(3, 1);
        else if (mode == L"login") {
            ShowPage(3, 1);
            accountPage_->OpenLogin();
        }
        else if (mode == L"about") ShowPage(5, 0);
        else if (mode == L"license") ShowPage(5, 1);
        else if (mode == L"mods") ShowPage(2, 1);
        else if (mode == L"game") ShowPage(2, 0);
        else if (mode == L"panel") {
            ShowPage(2, 1);
            std::shared_ptr<vcui::Stack> holder;
            auto card = OpenPanel(surface_, L"测试面板", 640.f, 420.f, nullptr, &holder);
            if (card && holder) {
                holder->Add(Caption(L"这是一个居中的弹出面板，用来核对布局。"));
                auto scroll = std::make_shared<vcui::ScrollView>();
                scroll->Grow = 1.f;
                auto list = vcui::VBox(0.f);
                for (int i = 0; i < 12; ++i) {
                    auto row = vcui::HBox(10.f);
                    row->Pad(10.f);
                    row->Add(Text(WFormat(L"示例条目 %d", i)));
                    auto sp = vcui::Spacer(0.f);
                    sp->Grow = 1.f;
                    row->Add(sp);
                    auto b = std::make_shared<vcui::Button>(L"下载");
                    b->W = 76.f;
                    row->Add(b);
                    list->Add(row);
                    list->Add(std::make_shared<vcui::Divider>());
                }
                scroll->Add(list);
                holder->Add(scroll);
            }
        }
    }
    int code = vcui::RunAppLoop();
    ClearNetCache();          // 关启动器：会话缓存（JSON + 图片）全清
    CoUninitialize();
    return code;
}

void App::ClearNetCache() {
    // 先停掉图标线程的读盘动作再删，避免删到一半又被写回
    ClearTree(HttpCacheDir());
    ClearTree(IconCacheDir());
    EnsureDir(HttpCacheDir());
    EnsureDir(IconCacheDir());
}

void App::SaveSettings() {
    std::string err;
    if (!::SaveSettings(settings_, &err) && !err.empty())
        ctx_.Toast(L"配置保存失败：" + Utf8ToWide(err), vcui::InfoTone::Warning);
}

namespace {

// 内容区底色要比标题栏的 kp 深一档，浅色主题尤其要明显，才能靠"颜色差 + 细线"分家
D2D1_COLOR_F ShellBg(const vcui::Tokens& t) {
    return t.dark ? vcui::Mix(t.kp, t.bj, .55f) : t.xt;
}

// 二级导航栏：夹在标题栏(kp)和内容区底(ShellBg)中间的一档，只是"微微差一点"
D2D1_COLOR_F SidebarBg(const vcui::Tokens& t) {
    return vcui::Mix(t.kp, ShellBg(t), .45f);
}

// 状态栏：比侧栏再压一档，深色下贴底、浅色下也不至于和内容区糊成一片
D2D1_COLOR_F StatusBg(const vcui::Tokens& t) {
    return t.dark ? vcui::Mix(t.kp, t.bj, .85f) : vcui::Mix(t.kp, t.xt, .65f);
}

} // namespace

// 侧栏右缘：左边一条 1px 分隔线，右侧几像素黑色渐变淡出，做出"侧栏浮在内容区上"的投影。
// 只占 10px 宽，不参与命中（不设 Interactive），鼠标穿透到内容区。
class AppSideShadow : public vcui::Widget {
public:
    static constexpr float kWidth = 14.f;

    AppSideShadow() { W = kWidth; }

    D2D1_SIZE_F Desired(float, float availH) override {
        return D2D1::SizeF(W > 0 ? W : kWidth, availH);
    }

    void Paint(vcui::PaintCtx& ctx) override {
        D2D1_RECT_F fr = Frame();
        if (fr.right <= fr.left) return;

        // 竖线用主题分隔线色，避免和投影糊成一条黑边
        ctx.g.Line(fr.left + .5f, fr.top, fr.left + .5f, fr.bottom, ctx.t.divider, ctx.Hair());

        // 多层停靠模拟软阴影：贴边最重，之后平滑衰减到透明
        float a = ctx.t.dark ? .34f : .15f;
        D2D1_RECT_F band{ fr.left + 1.f, fr.top, fr.right, fr.bottom };
        ctx.g.FillLinear(band, {
            { 0.f,   vcui::C(0, 0, 0, a) },
            { .18f,  vcui::C(0, 0, 0, a * .62f) },
            { .42f,  vcui::C(0, 0, 0, a * .28f) },
            { .72f,  vcui::C(0, 0, 0, a * .09f) },
            { 1.f,   vcui::C(0, 0, 0, 0) } }, true);
    }
};

// 状态栏片段：自绘一行小字。Label 在状态栏里点不动（命中被 HBox 吃掉），
// 这里直接按文字宽度出尺寸，可点时给手型光标和悬停底色。
// 命中区比文字左右各宽出几像素，点起来不至于太挑。
StatusText::StatusText(std::wstring text) : Text(std::move(text)) {
    H = 16.f;
    PL = 5.f;
    PR = 5.f;
}

D2D1_SIZE_F StatusText::Desired(float, float) {
    const float w = Tw(Text, Size, Weight) + PL + PR;
    return D2D1::SizeF(W > 0 ? W : w, H);
}

vcui::CursorShape StatusText::CursorAt(float, float) const {
    return Clickable ? vcui::CursorShape::Hand : vcui::CursorShape::Arrow;
}

// "最近操作"这类片段带生命周期：进来 0.2s 淡入，最后 0.5s 淡出，到点清空。
bool StatusText::Update(float dt) {
    bool changed = vcui::Widget::Update(dt);
    if (Life >= 0.f) {
        changed = true;         // 倒计时期间必须保持帧循环（否则淡出到一半就停）
        Life -= dt;
        if (Life <= 0.f) {
            Life = -1.f;
            Opacity = 1.f;
            Text.clear();
            changed = true;
        } else {
            float fadeIn = (5.f - Life) / .2f;
            float fadeOut = Life / .5f;
            float a = (std::min)(1.f, (std::min)(fadeIn, fadeOut));
            if (a < 0.f) a = 0.f;
            if (Opacity != a) {
                Opacity = a;
                changed = true;
            }
        }
    }
    return changed;
}

void StatusText::Paint(vcui::PaintCtx& ctx) {
    const bool inverted = Ink.a > 0.f;
    if (Clickable && Hover && fr_.right > fr_.left) {
        D2D1_COLOR_F hl = inverted ? Ink : ctx.t.xt;
        hl.a = inverted ? .14f : (ctx.t.dark ? .16f : .07f);
        D2D1_RECT_F rc{ fr_.left, fr_.top - 2.f, fr_.right, fr_.bottom + 2.f };
        ctx.g.RoundRect(rc, 2.f, hl);
    }
    if (Text.empty()) return;
    D2D1_RECT_F tr{ fr_.left + PL, fr_.top, fr_.right - PR, fr_.bottom };
    ctx.g.Text(tr, Text, Size, Weight, inverted ? Ink : ctx.t.wz, Align, vcui::VAlign::Middle);
}

namespace {

// "C:\Games\.minecraft" 的最后一段；空就回退占位名
std::wstring LeafName(const std::wstring& path, const std::wstring& fallback) {
    size_t cut = path.find_last_of(L"\\/");
    std::wstring leaf = (cut == std::wstring::npos) ? path : path.substr(cut + 1);
    return leaf.empty() ? fallback : leaf;
}

// 请求地址 → 状态栏文案
std::wstring RequestLabel(const std::wstring& url) {
    const std::wstring low = ToLower(url);
    if (low.find(L"modrinth") != std::wstring::npos) return L"正在请求 Modrinth…";
    if (low.find(L"bmclapi") != std::wstring::npos) return L"正在请求 BMCLAPI…";
    if (low.find(L"mojang") != std::wstring::npos || low.find(L"piston") != std::wstring::npos)
        return L"正在请求 Mojang…";
    return L"正在请求网络…";
}

// 一级页 → 侧栏二级导航。主页不带二级导航（侧栏整条收起）。
std::vector<std::wstring> SubNavFor(int page) {
    switch (page) {
    case 1: return { L"已安装", L"启动记录" };
    case 2: return DownloadPage::SubNav();
    case 3: return { L"Java", L"账户" };
    case 4: return { L"界面设置", L"全局设置" };
    case 5: return { L"关于", L"许可" };
    default: return {};
    }
}

} // namespace

void App::ApplyUi() {
    ApplyThemeToSurface(settings_, surface_);
    if (body_ && surface_) {
        // 主题变了底色也要跟着换，否则浅/深切换后分层关系会反掉
        const vcui::Tokens& t = surface_->Th().t;
        body_->Bg = ShellBg(t);
        if (sidebar_) sidebar_->Bg = SidebarBg(t);
        if (statusBar_) statusBar_->Bg = StatusBg(t);   // 深/浅色切换后状态栏底色要跟着换
        if (statusUser_) statusUser_->Text = settings_.accountName.empty() ? L"Player" : settings_.accountName;
        ApplyStatusColors();                            // 强调色状态栏与文字反色
        surface_->Invalidate();
    }
}

void App::ApplyStatusColors() {
    if (!surface_) return;
    const vcui::Tokens& t = surface_->Th().t;
    bool accent = settings_.statusAccent;

    if (statusBar_) statusBar_->Bg = accent ? AccentColor(settings_) : StatusBg(t);

    // 强调色铺底时文字反色：亮底色用深字、暗底色用白字
    D2D1_COLOR_F ink{ 0, 0, 0, 0 };
    if (accent) {
        D2D1_COLOR_F a = AccentColor(settings_);
        float lum = .2126f * a.r + .7152f * a.g + .0722f * a.b;
        ink = lum > .55f ? D2D1::ColorF(0x1E1E1E) : D2D1::ColorF(0xFFFFFF);
    }
    for (auto* s : { &statusState_, &statusUser_, &statusOp_, &statusTask_, &statusDir_ })
        if (*s) (*s)->Ink = ink;
}

void App::Build(vcui::Widget& body) {
    body_ = &body;
    if (body.Host) body.Bg = ShellBg(body.Host->Th().t);
    body.Add(std::make_shared<vcui::Divider>());

    auto shell = vcui::HBox(0.f);
    shell->Grow = 1.f;

    sidebar_ = vcui::VBox(2.f);
    sidebar_->W = 196.f;
    sidebar_->Pad(14.f);
    sidebar_->Bg = SidebarBg(body.Host->Th().t);   // 比标题栏略深，和内容区之间还有一条竖线

    sidebarLine_ = std::make_shared<AppSideShadow>();
    sidebarLine_->W = AppSideShadow::kWidth;

    content_ = vcui::VBox(10.f);
    content_->Grow = 1.f;
    content_->Pad(16.f);

    shell->Add(sidebar_);
    shell->Add(sidebarLine_);
    shell->Add(content_);
    body.Add(shell);
    BuildStatusBar(body);
    RebuildSidebar();
    RebuildContent();
}

// 底部状态栏：状态 / 账户 / 最近操作 / 任务 …………… 右对齐 Minecraft 目录
void App::BuildStatusBar(vcui::Widget& body) {
    body.Add(std::make_shared<vcui::Divider>());

    auto bar = vcui::HBox(0.f);
    bar->H = 30.f;
    bar->Cross = vcui::Stack::Center;
    statusBar_ = bar;

    // Spacer() 造的是竖向留白（W=0、H=size），横向间距要自己给宽
    auto gap = [](float w) { auto g = std::make_shared<vcui::Widget>(); g->W = w; return g; };
    bar->Add(gap(12.f));

    auto seg = [&](std::shared_ptr<StatusText> l, float leftGap, bool clickable) {
        bar->Add(gap(leftGap));
        l->Clickable = clickable;
        l->Interactive = clickable;
        bar->Add(l);
        return l;
    };

    statusState_ = std::make_shared<StatusText>(L"就绪");
    seg(statusState_, 12.f, false);

    statusUser_ = std::make_shared<StatusText>(
        settings_.accountName.empty() ? L"Player" : settings_.accountName);
    statusUser_->Tooltip = L"离线账户（可在 管理 · Java 之外的「账户」页改名）";
    seg(statusUser_, 18.f, false);

    auto self = this;

    // 左 3：任务。点它进任务页（任务页还没做，先提示）
    statusTask_ = std::make_shared<StatusText>(L"任务 0");
    statusTask_->Tooltip = L"下载任务（任务页还没做）";
    statusTask_->Clicked = [self] {
        self->ctx_.Toast(L"任务页还没做，先在下载页看进度", vcui::InfoTone::Info);
    };
    seg(statusTask_, 18.f, true);

    // 左 4：最近操作（替代 toast）。5 秒后自己淡出，点它可以把原文再弹一遍
    statusOp_ = std::make_shared<StatusText>(L"");
    statusOp_->Tooltip = L"点击查看这条提示";
    statusOp_->Clicked = [self] {
        if (self->opText_.empty()) return;
        if (self->surface_) self->surface_->ShowInfoBar(self->opText_, self->opTone_, 3.5f);
    };
    seg(statusOp_, 18.f, true);

    auto sp = std::make_shared<vcui::Widget>();
    sp->Grow = 1.f;
    bar->Add(sp);

    // 右对齐：当前 Minecraft 目录名
    statusDir_ = std::make_shared<StatusText>(LeafName(CurrentRootPath(settings_), L".minecraft"));
    statusDir_->Align = vcui::HAlign::Right;
    statusDir_->Tooltip = CurrentRootPath(settings_);
    seg(statusDir_, 0.f, false);
    bar->Add(gap(16.f));

    body.Add(bar);
    ApplyStatusColors();     // 默认跟主题；界面设置里开了"状态栏使用强调色"就换成强调色

    // 操作提示统一进状态栏第 3 格；点它再弹 InfoBar
    ctx_.operation = [this](const std::wstring& text, vcui::InfoTone tone) {
        SetOperation(text, tone);
    };
    // 每个请求都刷新"正在请求 xxx"
    http_.onRequest = [this](const std::wstring& url) {
        lastRequestText_ = RequestLabel(url);
        if (statusState_) statusState_->Text = lastRequestText_;
    };
}

void App::SetOperation(const std::wstring& text, vcui::InfoTone tone) {
    opText_ = text;
    opTone_ = tone;
    if (statusOp_) {
        statusOp_->Text = text;
        statusOp_->Tooltip = text + L"（点击重看）";
        statusOp_->Opacity = 0.f;
        statusOp_->Life = 5.f;      // 5 秒后淡出清空
    }
    if (surface_) {
        surface_->PlayAnim();      // 淡入淡出要跑帧
        surface_->Invalidate();
    }
}

void App::InitTitleBar() {
    if (!surface_ || !surface_->Root()) return;
    const auto& kids = surface_->Root()->Children();
    if (kids.empty()) return;
    title_ = dynamic_cast<vcui::TitleBar*>(kids.front().get());
    if (!title_) return;

    title_->Fill = true;          // 标题栏铺满 kp，与内容区底色拉开层次
    title_->NavItems = { L"主页", L"版本", L"下载", L"管理", L"设置", L"更多" };
    title_->NavCentered = true;
    title_->NavGap = 4.f;
    title_->NavIndex = page_;
    title_->NavChanged = [this](int idx) { ShowPage(idx, 0); };
    surface_->Invalidate();
}

void App::SyncTitleNav() {
    if (title_) title_->NavIndex = page_;
}

// 侧栏只承载"当前一级页"的子导航；主页不需要二级导航，直接收起。
void App::RebuildSidebar() {
    if (!sidebar_) return;
    sidebar_->Clear();
    navItems_.clear();
    auto self = this;

    if (page_ == 0) {
        sidebar_->Visible = false;
        if (sidebarLine_) sidebarLine_->Visible = false;
        return;
    }
    sidebar_->Visible = true;
    if (sidebarLine_) sidebarLine_->Visible = true;

    const auto subs = SubNavFor(page_);
    // 一级页的标题已经在顶部标签栏里，侧栏不再重复一行 "xx · 子导航"
    for (size_t k = 0; k < subs.size(); ++k) {
        auto sub = std::make_shared<vcui::NavItem>(subs[k], L"");
        sub->H = 34.f;
        sub->Active = (sub_ == (int)k);
        int sk = (int)k;
        int pg = page_;
        sub->Clicked = [self, pg, sk] { self->ShowPage(pg, sk); };
        sidebar_->Add(sub);
        navItems_.push_back(sub);
    }

    auto sp = vcui::Spacer(0.f);
    sp->Grow = 1.f;
    sidebar_->Add(sp);

    // 二级导航也错峰入场，和内容区的落位节奏错开一点
    if (settings_.animations) {
        float d = 0.f;
        for (auto& k : sidebar_->Children()) {
            if (!dynamic_cast<vcui::NavItem*>(k.get())) continue;
            k->FadeIn(.18f, 4.f, d);
            d = (std::min)(d + .025f, .10f);
        }
    }
}

void App::RebuildContent() {
    if (!content_) return;
    content_->Clear();
    auto self = this;

    // 常驻泵：网络回调、下载进度、底栏刷新。忙时保持 60fps。
    auto pump = std::make_shared<FramePump>();
    pump->Pump = [self]() -> bool {
        bool fired = self->http_.Pump();
        self->icons_->Pump();
        self->dl_->Tick();
        bool changed = fired;
        if (self->accountPage_ && self->accountPage_->Tick()) changed = true;
        for (auto& ev : self->dl_->TakeEvents()) {
            changed = true;
            if (ev.kind == DownloadEvent::Completed) {
                self->ctx_.Toast(L"下载完成：" + ev.path, vcui::InfoTone::Success);
            } else if (ev.kind == DownloadEvent::Failed) {
                self->ctx_.Toast(L"下载失败：" + ev.message, vcui::InfoTone::Danger);
            }
        }

        // 状态栏文案变了才重绘，别为了后台泵每帧整窗刷
        auto prog = self->dl_->Progress();
        auto stateText = self->http_.Busy()
            ? (self->lastRequestText_.empty() ? std::wstring(L"正在请求网络…") : self->lastRequestText_)
            : (self->icons_->Busy() ? std::wstring(L"正在加载图标…") : std::wstring(L"就绪"));
        if (self->statusState_ && self->statusState_->Text != stateText) {
            self->statusState_->Text = stateText;
            changed = true;
            if (self->surface_) self->surface_->InvalidateRectD(self->statusState_->Frame());
        }
        std::wstring taskText = (prog.active || prog.queued)
            ? WFormat(L"正在下载 %u 个 · 队列 %u · %s", prog.active, prog.queued,
                      (HumanBytes((long long)prog.bytesPerSec) + L"/s").c_str())
            : std::wstring(L"任务 0");
        if (self->statusTask_ && self->statusTask_->Text != taskText) {
            self->statusTask_->Text = taskText;
            changed = true;
            if (self->surface_) self->surface_->InvalidateRectD(self->statusTask_->Frame());
        }
        return self->http_.Busy() || self->icons_->Busy() || self->dl_->Busy() || changed;
    };
    content_->Add(pump);
    std::shared_ptr<vcui::Widget> pageRoot;
    if (page_ == 2) {
        pageRoot = download_->Build(sub_);
    } else if (page_ == 4) {
        pageRoot = settingsPage_->Build(sub_);
    } else if (page_ == 5) {
        pageRoot = aboutPage_->Build(sub_);
    } else if (page_ == 1) {
        pageRoot = BuildPlaceholder(L"版本", { L"已安装版本一览（读取 .minecraft\\versions）",
                                              L"启动配置：Java、内存、参数",
                                              L"启动记录与崩溃日志" });
    } else if (page_ == 3) {
        if (sub_ == 0) {
            pageRoot = javaPage_->Build(sub_);
        } else {
            pageRoot = accountPage_->Build(0);
        }
    } else {
        auto home = vcui::VBox(12.f);
        home->Grow = 1.f;
        auto card = std::make_shared<vcui::Card>();
        auto col = vcui::VBox(8.f);
        col->Pad(20.f);
        col->Add(Text(L"当前游戏目录", vcui::Type::Subtitle2));
        col->Add(Caption(CurrentRootPath(settings_)));
        col->Add(std::make_shared<vcui::Divider>());
        col->Add(Text(L"下载源", vcui::Type::Subtitle2));
        col->Add(Caption(settings_.mirror == L"mojang"
                             ? L"游戏：Mojang 官方；其余类别：Modrinth"
                             : L"游戏：BMCLAPI 镜像；其余类别：Modrinth"));
        auto go = std::make_shared<vcui::Button>(L"去下载页", true);
        go->W = 140.f;
        go->Clicked = [self] { self->ShowPage(2, 0); };
        col->Add(go);
        card->Add(col);
        home->Add(card);
        pageRoot = home;
    }
    content_->Add(pageRoot);
    // 切页入场：内容整体淡入并轻微上移，列表里的每一行错峰落下（设置里可关）
    if (settings_.animations) pageRoot->StaggerIn(.22f, 10.f, .035f, .18f);

    // 底部的空闲/速度已改为全局状态栏（见 BuildStatusBar），这里不再加页脚
}

void App::UpdateSidebarActive() {
    for (size_t k = 0; k < navItems_.size(); ++k)
        navItems_[k]->Active = (sub_ == (int)k);
}

// 占位页：标题 + 一排"计划中的能力"，等对应模块落地再换掉
std::shared_ptr<vcui::Widget> App::BuildPlaceholder(const std::wstring& title,
                                                    const std::vector<std::wstring>& items) {
    auto page = vcui::VBox(12.f);
    page->Grow = 1.f;
    auto card = std::make_shared<vcui::Card>();
    auto col = vcui::VBox(10.f);
    col->Pad(20.f);
    col->Add(Text(title, vcui::Type::Subtitle2));
    for (const auto& it : items) {
        auto row = vcui::HBox(6.f);
        row->Cross = vcui::Stack::Center;
        row->Add(Caption(L"•"));
        row->Add(Caption(it));
        col->Add(row);
    }
    col->Add(Caption(L"这一页还没做，先把导航位置占上。"));
    card->Add(col);
    page->Add(card);
    return page;
}

void App::ShowPage(int page, int sub) {
    if (page == page_ && sub == sub_) return;
    const bool pageChanged = (page != page_);
    // 下载页整块都是网络数据：进/出这一页（含 Tab 切换）都把会话缓存清一遍，
    // 免得拿到上一次浏览时的旧 JSON / 旧图片
    if (page == 2 || page_ == 2) ClearNetCache();
    page_ = page;
    sub_ = sub;
    SyncTitleNav();

    // 只换二级导航项时不动侧栏结构：不然已有元素会重新走一遍入场动画，看着像"闪一下"
    if (pageChanged) {
        RebuildSidebar();
    } else {
        UpdateSidebarActive();
        if (sidebar_) sidebar_->Visible = true;
    }
    RebuildContent();
    if (surface_) surface_->Invalidate();
}

} // namespace vcl
















