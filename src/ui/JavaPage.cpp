#include "ui/JavaPage.h"
#include "core/JavaCatalog.h"
#include "core/JavaRuntime.h"
#include "core/Paths.h"
#include "core/Text.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <thread>

namespace vcl {

namespace {

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

D2D1_COLOR_F T2(const UiCtx& c) { return c.surface ? c.surface->Th().t.text2 : vcui::Hex(0x606060); }
D2D1_COLOR_F T3(const UiCtx& c) { return c.surface ? c.surface->Th().t.text3 : vcui::Hex(0x909090); }
D2D1_COLOR_F Acc(const UiCtx& c) { return c.surface ? c.surface->Th().t.accent : vcui::Hex(0x0071EA); }

bool SamePath(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || b.empty()) return false;
    return Lower(a) == Lower(b);
}

std::shared_ptr<vcui::Label> Lbl(const std::wstring& s, float size, D2D1_COLOR_F col,
                                 float w = -1.f, bool ellipsis = false) {
    auto l = std::make_shared<vcui::Label>(s, size, 400);
    l->Color = col;
    l->Ellipsis = ellipsis;
    if (w > 0) l->W = w;
    return l;
}

// Fluent 按钮，但左边带一个 SVG 图标（图标用 Gfx::SvgPath 画，不依赖字体图标）
class IconTextButton : public vcui::Widget {
public:
    std::wstring Text, Path;
    bool Primary = false;
    float IconSize = 15.f;

    IconTextButton(std::wstring text, std::wstring path, bool primary = false)
        : Text(std::move(text)), Path(std::move(path)), Primary(primary) {
        H = 32.f;
        Interactive = true;
        FocusRing = false;
    }

    D2D1_SIZE_F Desired(float, float) override {
        float tw = Tw(Text, 13.f, 400);
        return D2D1::SizeF(24.f + IconSize + 6.f + tw, H > 0 ? H : 32.f);
    }
    vcui::CursorShape CursorAt(float, float) const override { return vcui::CursorShape::Hand; }
    bool Focusable() const override { return true; }
    void OnKeyDown(int vk) override {
        if (vk == VK_SPACE || vk == VK_RETURN) OnClick();
    }
    void Paint(vcui::PaintCtx& ctx) override {
        const vcui::Tokens& t = ctx.t;
        float hv = HoverA(), pv = PressA();
        D2D1_COLOR_F bg, ink;
        if (Primary) {
            bg = vcui::Mix(t.accent, hv > 0.f ? t.accentHover : t.accent, hv);
            if (pv > .01f) bg = vcui::Mix(bg, t.accentPress, pv);
            ink = t.onAccent;
        } else {
            bg = vcui::Mix(vcui::Ctrl(t), vcui::CtrlHover(t), hv);
            ink = t.wz;
        }
        ctx.g.RoundRect(fr_, 4.f, bg);
        if (!Primary) ctx.g.StrokeRect(fr_, 4.f, t.border, 1.f);

        float cy = (fr_.top + fr_.bottom) * .5f;
        D2D1_RECT_F ir{ fr_.left + 12.f, cy - IconSize * .5f, fr_.left + 12.f + IconSize,
                        cy + IconSize * .5f };
        ctx.g.SvgPath(ir, Path, ink, 1.4f, false);
        D2D1_RECT_F tr{ ir.right + 6.f, fr_.top, fr_.right - 10.f, fr_.bottom };
        ctx.g.Text(tr, Text, 13.f, 400, ink, vcui::HAlign::Left, vcui::VAlign::Middle);
    }
};

// Fluent 2 的线性图标（20 格坐标，SvgPath 会按框缩放）
const wchar_t* kIcoSearch = L"M16 9.5C16 13.09 13.09 16 9.5 16C5.91 16 3 13.09 3 9.5C3 5.91 5.91 3 9.5 3C13.09 3 16 5.91 16 9.5ZM14.6 14.6 18 18";
const wchar_t* kIcoPlus = L"M10 3.2v13.6M3.2 10h13.6";
const wchar_t* kIcoDownload = L"M10 3.2v9.6M10 12.8 5.8 8.6M10 12.8 14.2 8.6M3.2 16.8h13.6";
const wchar_t* kIcoFolder = L"M3 8.4V6.4H7.5L9.5 8.4H17V15.6H3Z";
// 一行 Java：不加描边，靠选中底色 / 悬停描边区分（Fluent 列表项的做法）
class JavaRow : public vcui::Widget {
public:
    bool Selected = false;
    JavaRow() { H = 48.f; Interactive = true; }
    D2D1_SIZE_F Desired(float availW, float) override { return D2D1::SizeF(availW, H); }
    void LayoutChildren() override {
        if (kids_.empty()) return;
        kids_.front()->Arrange(Content());
    }
    vcui::CursorShape CursorAt(float, float) const override { return vcui::CursorShape::Hand; }
    void Paint(vcui::PaintCtx& ctx) override {
        if (Selected) ctx.g.RoundRect(fr_, 3.f, vcui::A(ctx.t.accent, ctx.t.dark ? .20f : .10f));
        else if (HoverA() > .01f) ctx.g.RoundRect(fr_, 3.f, vcui::Overlay(ctx.t, (ctx.t.dark ? .07f : .04f) * HoverA()));
        if (HoverA() > .01f)
            ctx.g.StrokeRect(fr_, 3.f, vcui::A(ctx.t.accent, .55f * HoverA()), 1.f);
    }
};

bool PickJavaExe(std::wstring& out) {
    wchar_t file[2048]{};
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.lpstrFilter = L"Java 可执行文件 (java.exe; javaw.exe)\0java.exe;javaw.exe\0可执行文件 (*.exe)\0*.exe\0\0";
    of.lpstrFile = file;
    of.nMaxFile = 2048;
    of.lpstrTitle = L"选择 java.exe 或 javaw.exe";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return false;
    out = file;
    return true;
}

// 用系统自带 bsdtar 解 zip（Win10 起内置），在后台线程里跑
bool ExtractZip(const std::wstring& zip, const std::wstring& dir) {
    EnsureDir(dir);
    std::wstring exe = L"C:\\Windows\\System32\\tar.exe";
    if (!FileExists(exe)) exe = L"tar.exe";
    std::wstring cmd = L"\"" + exe + L"\" -xf \"" + zip + L"\" -C \"" + dir + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}

} // namespace

struct JavaPage::State {
    std::vector<JavaRuntime> runtimes;

    // 页面
    std::shared_ptr<vcui::Stack> rows;
    std::shared_ptr<vcui::Label> count;
    std::shared_ptr<vcui::Label> usingLine;

    // 下载弹窗
    bool panelOpen = false;
    std::shared_ptr<vcui::Dropdown> vendorDD, majorDD, kindDD;
    std::shared_ptr<vcui::Label> info;
    std::shared_ptr<vcui::Spinner> spin;
    std::shared_ptr<vcui::Progress> prog;
    std::shared_ptr<vcui::Widget> dlBtn;

    int vendor = 0;
    std::vector<int> majors{ 25, 21, 17, 11, 8 };
    bool kindJdk = true;
    bool loadingMajors = false;
    bool loadingRelease = false;
    JavaRelease pending{};
    bool hasPending = false;

    bool downloading = false;
    float sinceStart = 0.f;
    std::wstring zipPath, targetDir;
    bool extracting = false;
    std::atomic<bool> extractDone{ false };
    bool extractOk = false;
    std::atomic<bool> scanDone{ false };
};

std::vector<std::wstring> JavaPage::SubNav() {
    return { L"实例", L"Mod", L"存档", L"Java" };
}

JavaPage::JavaPage(UiCtx* ctx) : ctx_(ctx) { state_ = std::make_shared<State>(); }

std::shared_ptr<vcui::Widget> JavaPage::Build(int sub) {
    (void)sub;
    auto st = state_;
    auto self = this;

    st->rows.reset();
    st->count.reset();
    st->usingLine.reset();

    auto page = vcui::VBox(12.f);
    page->Grow = 1.f;

    // ---------------- 唯一一张卡片：标题 + 列表 + 底部动作 ----------------
    auto inner = vcui::VBox(0.f);

    auto head = vcui::HBox(8.f);
    head->PT = 12.f;
    head->PB = 10.f;
    head->PL = 16.f;
    head->PR = 16.f;
    head->W = -1.f;
    head->Cross = vcui::Stack::Center;
    head->Add(Lbl(L"Java 运行时", 13.f, T2(*ctx_)));
    auto sp = std::make_shared<vcui::Widget>();
    sp->Grow = 1.f;
    head->Add(sp);
    st->usingLine.reset();
    auto gap = std::make_shared<vcui::Widget>();
    gap->W = 12.f;
    head->Add(gap);
    st->count = Lbl(L"", 12.f, T3(*ctx_));
    head->Add(st->count);
    inner->Add(head);

    inner->Add(std::make_shared<vcui::Divider>());

    auto listWrap = vcui::VBox(0.f);
    listWrap->PT = 4.f;
    listWrap->PB = 4.f;
    listWrap->PL = 6.f;
    listWrap->PR = 6.f;
    st->rows = vcui::VBox(0.f);
    st->rows->W = -1.f;
    listWrap->Add(st->rows);
    inner->Add(listWrap);

    inner->Add(std::make_shared<vcui::Divider>());

    auto foot = vcui::HBox(8.f);
    foot->PT = 10.f;
    foot->PB = 10.f;
    foot->PL = 12.f;
    foot->PR = 12.f;
    foot->W = -1.f;
    foot->Cross = vcui::Stack::Center;

    auto scan = std::make_shared<IconTextButton>(L"扫描 PATH", kIcoSearch);
    scan->Clicked = [self] { self->ScanPath(true); };

    auto add = std::make_shared<IconTextButton>(L"添加 Java…", kIcoPlus);

    auto dl = std::make_shared<IconTextButton>(L"下载 Java", kIcoDownload, true);
    dl->Clicked = [self] { self->OpenDownload(); };

    auto openDir = std::make_shared<IconTextButton>(L"打开 Java 目录", kIcoFolder);
    openDir->Clicked = [self] {
        EnsureDir(JavaInstallDir());
        ShellExecuteW(nullptr, L"open", JavaInstallDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };

    foot->Add(scan);
    foot->Add(add);
    foot->Add(dl);
    auto spF = std::make_shared<vcui::Widget>();
    spF->Grow = 1.f;
    foot->Add(spF);
    foot->Add(openDir);
    inner->Add(foot);

    add->Clicked = [self] {
        std::wstring exe;
        if (!PickJavaExe(exe)) return;
        JavaRuntime rt;
        if (!JavaInspect(exe, rt, L"手动添加")) {
            self->ctx_->Toast(L"这个文件不是 Java 运行时", vcui::InfoTone::Danger);
            return;
        }
        auto& cfg = *self->ctx_->settings;
        bool known = false;
        for (const auto& p : cfg.javaExtra)
            if (SamePath(p, rt.home)) known = true;
        if (!known) {
            cfg.javaExtra.push_back(rt.home);
            if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        }
        self->ScanPath(false);
        self->ctx_->Toast(L"已添加 Java " + rt.version + L"（" + rt.vendor + L"）", vcui::InfoTone::Success);
    };

    auto card = std::make_shared<vcui::Card>();
    card->Shadow = false;
    card->Border = true;
    card->Radius = 4.f;
    card->Pad(0.f);
    card->Add(inner);
    page->Add(card);
    page->MB = 24.f;

    self->ScanPath(false);
    self->RefreshList();

    // 下载不算主线程活：网络与解压都在后台，这里只推进 UI
    auto pump = std::make_shared<FramePump>();
    pump->Pump = [self]() -> bool {
        auto s = self->state_;
        return self->TickDownload();
    };
    page->Add(pump);

    auto root = vcui::VBox(0.f);
    root->Grow = 1.f;
    auto scroll = std::make_shared<vcui::ScrollView>();
    scroll->Grow = 1.f;
    scroll->Add(page);
    root->Add(scroll);
    return root;
}

void JavaPage::ScanPath(bool notify) {
    auto st = state_;
    st->runtimes = JavaScan(ctx_->settings->javaExtra);
    auto& cfg = *ctx_->settings;
    if (cfg.javaPath.empty() && !st->runtimes.empty()) {
        for (const auto& rt : st->runtimes) {
            if (rt.jdk && rt.x64) {
                cfg.javaPath = rt.exe;
                if (ctx_->saveSettings) ctx_->saveSettings();
                break;
            }
        }
    }
    RefreshList();
    if (notify)
        ctx_->Toast(st->runtimes.empty() ? L"PATH 里没有找到 Java"
                                        : WFormat(L"找到 %d 个 Java", (int)st->runtimes.size()),
                    vcui::InfoTone::Info);
}

void JavaPage::RefreshList() {
    auto st = state_;
    if (!st->rows) return;
    auto& cfg = *ctx_->settings;
    st->rows->Clear();

    if (st->runtimes.empty()) {
        auto empty = vcui::VBox(0.f);
        empty->PT = 10.f;
        empty->PB = 12.f;
        empty->PL = 10.f;
        empty->Add(Lbl(L"PATH 里没有 Java。可以「添加 Java…」指定一个，或「下载 Java」。", 12.f, T3(*ctx_)));
        st->rows->Add(empty);
    }

    for (size_t i = 0; i < st->runtimes.size(); ++i) {
        const JavaRuntime rt = st->runtimes[i];
        const bool inUse = SamePath(cfg.javaPath, rt.exe);

        auto row = std::make_shared<JavaRow>();
        row->W = -1.f;
        row->Selected = inUse;
        row->Tooltip = L"点击设为启动用 Java";

        auto line = vcui::HBox(10.f);
        line->W = -1.f;
        line->PT = 6.f;
        line->PB = 6.f;
        line->PL = 10.f;
        line->PR = 10.f;
        line->Cross = vcui::Stack::Center;

        auto col = vcui::VBox(1.f);
        col->Grow = 1.f;
        col->W = -1.f;
        std::wstring title = L"Java " + (rt.version.empty() ? std::wstring(L"未知") : rt.version);
        if (!rt.vendor.empty()) title += L" · " + rt.vendor;
        col->Add(Lbl(title, 14.f, D2D1::ColorF(0, 0, 0, 0), -1.f, true));
        col->Add(Lbl(rt.home, 12.f, T3(*ctx_), -1.f, true));
        line->Add(col);

        line->Add(Lbl(rt.jdk ? L"JDK" : L"JRE", 12.f, T2(*ctx_), 38.f));
        line->Add(Lbl(rt.x64 ? L"x64" : L"x86", 12.f, T3(*ctx_), 34.f));
        line->Add(Lbl(rt.lts ? L"LTS" : L"", 12.f, Acc(*ctx_), 32.f));

        auto state = Lbl(inUse ? L"使用中" : L"设为启动", 12.f, inUse ? Acc(*ctx_) : T3(*ctx_), 56.f);
        state->Align = vcui::HAlign::Right;
        line->Add(state);

        row->Add(line);
        auto self2 = this;
        row->Clicked = [self2, i] {
            auto s = self2->state_;
            if (i >= s->runtimes.size()) return;
            auto& cfg2 = *self2->ctx_->settings;
            if (SamePath(cfg2.javaPath, s->runtimes[i].exe)) return;
            cfg2.javaPath = s->runtimes[i].exe;
            if (self2->ctx_->saveSettings) self2->ctx_->saveSettings();
            self2->RefreshList();
            self2->ctx_->Toast(L"启动用 Java 已切换为 " + s->runtimes[i].vendor + L" " + s->runtimes[i].version,
                               vcui::InfoTone::Success);
        };
        st->rows->Add(row);
    }

    if (st->count) st->count->Text = st->runtimes.empty() ? L"" : WFormat(L"%d 个", (int)st->runtimes.size());

    if (st->usingLine) {
        const JavaRuntime* sel = nullptr;
        for (const auto& rt : st->runtimes)
            if (SamePath(cfg.javaPath, rt.exe)) {
                sel = &rt;
                break;
            }
        st->usingLine->Text = sel ? (L"启动用：" + sel->vendor + L" " + sel->version) : L"";
    }
}

// ---------------- 下载弹窗 ----------------

void JavaPage::OpenDownload() {
    auto st = state_;
    if (!ctx_->surface) return;

    auto self = this;
    std::shared_ptr<vcui::Stack> content;
    auto panel = OpenPanel(ctx_->surface, L"下载 Java", 440.f, -1.f,
                           [self] { self->state_->panelOpen = false; }, &content);
    if (!panel || !content) return;
    panel->Radius = 6.f;
    st->panelOpen = true;

    content->Add(Lbl(L"只列 Windows x64 的 LTS 版本，下载后自动解压到启动器的 java 目录。", 12.f, T3(*ctx_)));

    std::vector<std::wstring> vnames;
    for (const auto& v : JavaVendors()) vnames.push_back(v.name);
    st->vendorDD = std::make_shared<vcui::Dropdown>(vnames, st->vendor);
    st->vendorDD->W = -1.f;

    std::vector<std::wstring> mitems;
    for (int m : st->majors) mitems.push_back(L"Java " + std::to_wstring(m) + L"（LTS）");
    st->majorDD = std::make_shared<vcui::Dropdown>(mitems, 0);
    st->majorDD->W = -1.f;

    st->kindDD = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"JDK（含编译器）", L"JRE（仅运行环境）" }, st->kindJdk ? 0 : 1);
    st->kindDD->W = -1.f;
    st->kindDD->Enabled = JavaVendorHasJre(JavaVendors()[st->vendor].id);

    auto field = [](const std::wstring& title, std::shared_ptr<vcui::Widget> ctl) {
        auto col = vcui::VBox(4.f);
        col->W = -1.f;
        col->Add(Lbl(title, 12.f, D2D1::ColorF(0, 0, 0, 0)));
        col->Add(ctl);
        return col;
    };
    content->Add(field(L"发行商", st->vendorDD));
    content->Add(field(L"版本", st->majorDD));
    content->Add(field(L"类型", st->kindDD));

    st->info = Lbl(L"", 12.f, T3(*ctx_));
    st->info->Wrap = true;
    content->Add(st->info);

    st->prog = std::make_shared<vcui::Progress>(0.f);
    st->prog->W = -1.f;
    st->prog->Visible = false;
    content->Add(st->prog);


    auto actions = vcui::HBox(8.f);
    actions->W = -1.f;
    auto spA = std::make_shared<vcui::Widget>();
    spA->Grow = 1.f;
    actions->Add(spA);

    auto cancel = std::make_shared<vcui::Button>(L"关闭");
    cancel->W = 80.f;
    cancel->Clicked = [self, panel] {
        self->state_->panelOpen = false;
        if (self->ctx_->surface) self->ctx_->surface->RemoveOverlayDeferred(panel.get());
    };
    st->dlBtn = std::make_shared<IconTextButton>(L"下载并安装", kIcoDownload, true);
    actions->Add(cancel);
    actions->Add(st->dlBtn);
    content->Add(actions);

    st->vendorDD->Changed = [self](int idx) {
        auto s = self->state_;
        s->vendor = idx;
        s->hasPending = false;
        s->pending = JavaRelease{};
        const bool hasJre = JavaVendorHasJre(JavaVendors()[idx].id);
        if (s->kindDD) {
            s->kindDD->Enabled = hasJre;
            if (!hasJre) {
                s->kindDD->Selected = 0;
                s->kindJdk = true;
            }
        }
        if (s->info) s->info->Text = hasJre ? std::wstring() : std::wstring(L"该发行商只发布 JDK。");
        self->LoadMajors();
    };
    st->majorDD->Changed = [self](int) {
        self->state_->hasPending = false;
        self->FetchRelease();
    };
    st->kindDD->Changed = [self](int idx) {
        self->state_->kindJdk = (idx == 0);
        self->state_->hasPending = false;
        self->FetchRelease();
    };
    st->dlBtn->Clicked = [self] { self->StartInstall(); };

    // 打开就拉一次版本列表；网络在后台，界面不卡
    self->LoadMajors();
}

void JavaPage::LoadMajors() {
    auto st = state_;
    if (st->loadingMajors) return;
    const std::wstring vendorId = JavaVendors()[st->vendor].id;
    st->loadingMajors = true;
    const bool hasJre = JavaVendorHasJre(vendorId);
    if (st->info)
        st->info->Text = st->vendorDD && st->vendorDD->Selected >= 0
                             ? L"正在读取版本…"
                             : L"正在读取版本…";
    auto self = this;
    JavaFetchMajors(ctx_->http, vendorId, [self, hasJre](bool ok, std::vector<int> majors, const std::wstring& err) {
        auto s = self->state_;
        s->loadingMajors = false;
        if (!ok) {
            if (s->info) s->info->Text = L"版本列表读取失败：" + err;
            return;
        }
        if (majors.empty()) {
            if (s->info) s->info->Text = L"没有可下载的 LTS 版本。";
            return;
        }
        s->majors = majors;
        if (s->majorDD) {
            std::vector<std::wstring> items;
            for (int m : majors) items.push_back(L"Java " + std::to_wstring(m) + L"（LTS）");
            s->majorDD->Items = items;
            s->majorDD->Selected = 0;
        }
        (void)hasJre;
        self->FetchRelease();
    });
}

void JavaPage::FetchRelease() {
    auto st = state_;
    if (st->loadingRelease) return;
    if (!st->majorDD || st->majors.empty()) return;
    int idx = std::clamp(st->majorDD->Selected, 0, (int)st->majors.size() - 1);
    const int major = st->majors[(size_t)idx];
    const std::wstring vendorId = JavaVendors()[st->vendor].id;
    const bool jdk = st->kindJdk;
    st->loadingRelease = true;
    st->hasPending = false;
    if (st->info) st->info->Text = L"正在查询 Java " + std::to_wstring(major) + (jdk ? L" JDK…" : L" JRE…");
    auto self = this;
    JavaFetchRelease(ctx_->http, vendorId, major, jdk,
                     [self, jdk](bool ok, const JavaRelease& rel, const std::wstring& err) {
        auto s = self->state_;
        s->loadingRelease = false;
        if (!ok) {
            s->hasPending = false;
            if (s->info) s->info->Text = L"没有可用的包：" + err;
            return;
        }
        s->pending = rel;
        s->hasPending = true;
        if (s->info)
            s->info->Text = WFormat(L"%s %s · Java %d · %s · %s",
                                    rel.vendorName.c_str(), rel.version.c_str(), rel.major,
                                    jdk ? L"JDK" : L"JRE", HumanBytes(rel.size).c_str());
    });
}

void JavaPage::StartInstall() {
    auto st = state_;
    if (st->downloading || st->extracting) return;
    if (!st->hasPending) {
        ctx_->Toast(L"版本信息还没取到，稍等一下", vcui::InfoTone::Warning);
        return;
    }
    const JavaRelease rel = st->pending;
    std::wstring dlDir = JoinPath(JavaInstallDir(), L"_download");
    EnsureDir(JavaInstallDir());
    EnsureDir(dlDir);
    std::wstring name = rel.filename;
    if (name.empty())
        name = rel.vendor + L"-" + std::to_wstring(rel.major) + (rel.jdk ? L"-jdk" : L"-jre") + L".zip";
    st->zipPath = JoinPath(dlDir, name);
    st->targetDir = JoinPath(JavaInstallDir(), rel.vendor + L"-" + rel.version + (rel.jdk ? L"-jdk" : L"-jre"));
    if (!ctx_->dl->Enqueue(rel.url, st->zipPath, L"", rel.size)) {
        ctx_->Toast(L"下载入队失败", vcui::InfoTone::Danger);
        return;
    }
    st->downloading = true;
    st->sinceStart = 0.f;
    st->extracting = false;
    st->extractDone = false;
    if (st->prog) {
        st->prog->Visible = true;
        st->prog->Value = 0.f;
        st->prog->Indeterminate = false;
    }
    if (st->info) st->info->Text = rel.vendorName + L" " + rel.version + L" · 正在下载…";
}

bool JavaPage::TickDownload() {
    auto st = state_;
    bool busy = false;

    if (st->downloading) {
        busy = true;
        auto prog = ctx_->dl->Progress();
        st->sinceStart += 0.016f;
        if (st->prog) {
            if (prog.bytesTotal > 0) {
                st->prog->Indeterminate = false;
                float v = (float)((double)prog.bytesDone / (double)prog.bytesTotal);
                st->prog->Value = std::clamp(v, 0.f, 1.f);
            } else {
                st->prog->Indeterminate = true;
            }
        }
        if (st->info && prog.bytesTotal > 0)
            st->info->Text = WFormat(L"%s / %s · %s/s",
                                     HumanBytes((long long)prog.bytesDone).c_str(),
                                     HumanBytes((long long)prog.bytesTotal).c_str(),
                                     HumanBytes((long long)prog.bytesPerSec).c_str());
        if (!ctx_->dl->Busy() && st->sinceStart > .8f) {
            st->downloading = false;
            if (st->prog) {
                st->prog->Indeterminate = false;
                st->prog->Visible = false;
            }
            if (FileExists(st->zipPath)) {
                st->extracting = true;
                st->extractDone = false;
                std::wstring zip = st->zipPath, dir = st->targetDir;
                std::atomic<bool>* done = &st->extractDone;
                bool* ok = &st->extractOk;
                std::thread([zip, dir, done, ok] {
                    *ok = ExtractZip(zip, dir);
                    DeleteFileW(zip.c_str());
                    done->store(true);
                }).detach();
                if (st->info) st->info->Text = L"正在解压…";
            } else {
                if (st->info) st->info->Text = L"下载失败，检查网络后重试。";
                ctx_->Toast(L"Java 下载失败", vcui::InfoTone::Danger);
            }
        }
    }

    if (st->extracting) {
        busy = true;
        if (st->extractDone.load()) {
            st->extracting = false;
            if (st->extractOk) {
                std::vector<JavaRuntime> before = st->runtimes;
                st->runtimes = JavaScan(ctx_->settings->javaExtra);
                if (!st->runtimes.empty()) {
                    JavaRuntime* best = nullptr;
                    for (auto& rt : st->runtimes) {
                        bool known = false;
                        for (auto& old : before)
                            if (SamePath(old.home, rt.home)) known = true;
                        if (known) continue;
                        if (!best || JavaVersionCompare(rt, *best) > 0) best = &rt;
                    }
                    if (best) {
                        ctx_->settings->javaPath = best->exe;
                        if (ctx_->saveSettings) ctx_->saveSettings();
                        ctx_->Toast(L"Java " + best->version + L" 安装完成", vcui::InfoTone::Success);
                    }
                }
                RefreshList();
                if (st->info) st->info->Text = L"安装完成。";
            } else {
                if (st->info) st->info->Text = L"解压失败（安装包可能不完整）。";
                ctx_->Toast(L"解压失败", vcui::InfoTone::Danger);
            }
        }
    }

    if (st->loadingMajors || st->loadingRelease) busy = true;
    return busy;
}

} // namespace vcl
