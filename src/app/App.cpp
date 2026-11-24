#include "app/App.h"
#include "ui/DownloadPage.h"
#include "ui/SettingsPage.h"
#include "core/Paths.h"
#include "core/Text.h"

#include <windows.h>
#include <shellapi.h>

namespace vcl {

App::App() {
    LoadSettings(settings_);
    dl_ = std::make_unique<Downloader>(settings_.concurrency > 0 ? settings_.concurrency : 16);
    ctx_.http = &http_;
    ctx_.dl = dl_.get();
    ctx_.settings = &settings_;
    ctx_.saveSettings = [this] { SaveSettings(); };
    ctx_.applyUi = [this] { ApplyUi(); };
    download_ = std::make_unique<DownloadPage>(&ctx_);
    settingsPage_ = std::make_unique<SettingsPage>(&ctx_);
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
    InitTitleBar();
    ApplyUi();

    // 自检钩子：VCL_UI_TEST=settings|global|mods|panel 直接落到某一页，便于截屏核对
    if (const wchar_t* m = _wgetenv(L"VCL_UI_TEST")) {
        std::wstring mode = m;
        if (mode == L"home") ShowPage(0, 0);
        else if (mode == L"settings") ShowPage(2, 0);
        else if (mode == L"dl") {
            std::wstring out = JoinPath(std::wstring(_wgetenv(L"TEMP")), L"vcl-dl-test.json");
            bool ok = dl_->Enqueue(L"https://api.modrinth.com/v2/project/sodium", out, L"", 0);
        }
        else if (mode == L"global") ShowPage(2, 1);
        else if (mode == L"mods") ShowPage(1, 1);
        else if (mode == L"game") ShowPage(1, 0);
        else if (mode == L"panel") {
            ShowPage(1, 1);
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
    CoUninitialize();
    return code;
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

} // namespace

void App::ApplyUi() {
    ApplyThemeToSurface(settings_, surface_);
    if (body_ && surface_) {
        // 主题变了底色也要跟着换，否则浅/深切换后分层关系会反掉
        const vcui::Tokens& t = surface_->Th().t;
        body_->Bg = ShellBg(t);
        if (sidebar_) sidebar_->Bg = t.kp;
        surface_->Invalidate();
    }
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
    sidebar_->Bg = body.Host->Th().t.kp;   // 侧栏留白底，和内容区灰底之间那条竖线才看得见

    sidebarLine_ = std::make_shared<vcui::Divider>();
    sidebarLine_->Vertical = true;
    sidebarLine_->W = 1.f;

    content_ = vcui::VBox(10.f);
    content_->Grow = 1.f;
    content_->Pad(16.f);

    shell->Add(sidebar_);
    shell->Add(sidebarLine_);
    shell->Add(content_);
    body.Add(shell);
    RebuildSidebar();
    RebuildContent();
}

void App::InitTitleBar() {
    if (!surface_ || !surface_->Root()) return;
    const auto& kids = surface_->Root()->Children();
    if (kids.empty()) return;
    title_ = dynamic_cast<vcui::TitleBar*>(kids.front().get());
    if (!title_) return;

    title_->Fill = true;          // 标题栏铺满 kp，与内容区底色拉开层次
    title_->NavItems = { L"主页", L"下载", L"设置" };
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
    auto self = this;

    if (page_ == 0) {
        sidebar_->Visible = false;
        return;
    }
    sidebar_->Visible = true;

    const auto subs = (page_ == 1) ? DownloadPage::SubNav() : SettingsPage::SubNav();
    sidebar_->Add(Caption(page_ == 1 ? L"下载 · 子导航" : L"设置 · 子导航"));
    sidebar_->Add(vcui::Spacer(2.f));
    for (size_t k = 0; k < subs.size(); ++k) {
        auto sub = std::make_shared<vcui::NavItem>(subs[k], L"");
        sub->H = 34.f;
        sub->Active = (sub_ == (int)k);
        int sk = (int)k;
        int pg = page_;
        sub->Clicked = [self, pg, sk] { self->ShowPage(pg, sk); };
        sidebar_->Add(sub);
    }

    auto sp = vcui::Spacer(0.f);
    sp->Grow = 1.f;
    sidebar_->Add(sp);
    sidebar_->Add(std::make_shared<vcui::Divider>());
    auto cur = Caption(L"目录：" + CurrentRootPath(settings_));
    cur->Tooltip = CurrentRootPath(settings_);
    sidebar_->Add(cur);
}

void App::RebuildContent() {
    if (!content_) return;
    content_->Clear();
    auto self = this;

    // 常驻泵：网络回调、下载进度、底栏刷新。忙时保持 60fps。
    auto pump = std::make_shared<FramePump>();
    pump->Pump = [self]() -> bool {
        bool fired = self->http_.Pump();
        self->dl_->Tick();
        bool changed = fired;
        for (auto& ev : self->dl_->TakeEvents()) {
            changed = true;
            if (ev.kind == xgdl::EventKind::Completed) {
                self->ctx_.Toast(L"下载完成：" + ev.path, vcui::InfoTone::Success);
            } else if (ev.kind == xgdl::EventKind::Failed) {
                self->ctx_.Toast(L"下载失败：" + ev.message, vcui::InfoTone::Danger);
            }
        }

        auto prog = self->dl_->Progress();
        if (self->footerStatus_) {
            if (prog.active || prog.queued)
                self->footerStatus_->Text = WFormat(L"下载中 %u 个 · 队列 %u · 完成 %u · 失败 %u",
                                                    prog.active, prog.queued,
                                                    prog.completed, prog.failed);
            else if (prog.completed || prog.failed)
                self->footerStatus_->Text = WFormat(L"已完成 %u 个 · 失败 %u 个",
                                                    prog.completed, prog.failed);
            else
                self->footerStatus_->Text = L"空闲";
        }
        if (self->footerBar_) {
            float v = prog.bytesTotal ? (float)((double)prog.bytesDone / (double)prog.bytesTotal) : 0.f;
            if (v < 0.f) v = 0.f;
            if (v > 1.f) v = 1.f;
            self->footerBar_->Value = v;
        }
        if (self->footerSpeed_) {
            self->footerSpeed_->Text = prog.active
                ? HumanBytes((long long)prog.bytesPerSec) + L"/s"
                : std::wstring();
        }
        return self->http_.Busy() || self->dl_->Busy() || changed;
    };
    content_->Add(pump);
    if (page_ == 1) {
        content_->Add(download_->Build(sub_));
    } else if (page_ == 2) {
        content_->Add(settingsPage_->Build(sub_));
    } else {
        auto home = vcui::VBox(12.f);
        home->Grow = 1.f;
        home->Add(Caption(L"这一版先做下载与设置两页；主页/版本/管理在后续版本补齐。"));
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
        go->Clicked = [self] { self->ShowPage(1, 0); };
        col->Add(go);
        card->Add(col);
        home->Add(card);
        content_->Add(home);
    }

    auto foot = vcui::HBox(10.f);
    footerStatus_ = Caption(L"空闲");
    footerStatus_->Grow = 1.f;
    footerBar_ = nullptr;
    footerSpeed_ = Caption(L"");
    footerSpeed_->W = 96.f;
    footerSpeed_->Align = vcui::HAlign::Right;
    foot->Add(footerStatus_);
    foot->Add(footerSpeed_);
    content_->Add(foot);
}

void App::ShowPage(int page, int sub) {
    if (page == page_ && sub == sub_) return;
    page_ = page;
    sub_ = sub;
    SyncTitleNav();
    RebuildSidebar();
    RebuildContent();
    if (surface_) surface_->Invalidate();
}

} // namespace vcl










