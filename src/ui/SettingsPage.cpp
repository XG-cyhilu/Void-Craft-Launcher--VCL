#include "ui/SettingsPage.h"

#include <windows.h>
#include <shellapi.h>

#include "core/Paths.h"
#include "core/Text.h"

namespace vcl {

struct SettingsPage::State {
    std::shared_ptr<vcui::Input> cachePath;
    std::shared_ptr<vcui::Slider> opacity;
    std::shared_ptr<vcui::Label> opacityLabel;
    int sub = 0;
};

std::vector<std::wstring> SettingsPage::SubNav() {
    return { L"界面设置", L"全局设置" };
}

SettingsPage::SettingsPage(UiCtx* ctx) : ctx_(ctx) {}

// 1.2px 细分隔线：库里的 Divider 固定 1px，设置页的行距靠它切分
class ThinRule : public vcui::Widget {
public:
    float Thickness = 1.2f;
    ThinRule() { H = 1.2f; }
    D2D1_SIZE_F Desired(float availW, float) override { return D2D1::SizeF(availW, Thickness); }
    void Paint(vcui::PaintCtx& ctx) override {
        ctx.g.Line(fr_.left, fr_.top + .5f, fr_.right, fr_.top + .5f, ctx.t.divider, Thickness);
    }
};

// 一行：左标题 + 右控件。整页不套卡片，行直接落在背景色上。
static std::shared_ptr<vcui::Widget> Row(const std::wstring& title,
                                         std::shared_ptr<vcui::Widget> ctl,
                                         float ctlW = 200.f) {
    auto row = vcui::HBox(12.f);
    row->Cross = vcui::Stack::Center;
    row->PT = 10.f;
    row->PB = 10.f;
    auto col = vcui::VBox(0.f);
    col->Grow = 1.f;
    col->Add(Text(title));
    row->Add(col);
    if (ctl) {
        ctl->W = ctlW;
        row->Add(ctl);
    }
    return row;
}

// 一节：小标题 + 若干行（行间 1.2px 细线），节与节之间靠标题的上边距拉开
static void Section(vcui::Stack& page, const std::wstring& title,
                    const std::vector<std::shared_ptr<vcui::Widget>>& rows) {
    auto head = Text(title, vcui::Type::Subtitle2);
    head->MT = 14.f;
    head->MB = 2.f;
    page.Add(head);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) page.Add(std::make_shared<ThinRule>());
        page.Add(rows[i]);
    }
}

std::shared_ptr<vcui::Widget> SettingsPage::Build(int sub) {
    auto page = vcui::VBox(0.f);
    auto s = std::make_shared<State>();
    s->sub = sub;

    if (sub == 0) {
        BuildUi(s, *page);
    } else {
        BuildGlobal(s, *page);
    }

    // 设置项比一屏高，整页套一层滚动容器（内容按自身高度排）
    page->MB = 8.f;
    auto scroll = std::make_shared<vcui::ScrollView>();
    scroll->Grow = 1.f;
    scroll->Add(page);
    auto root = vcui::VBox(0.f);
    root->Grow = 1.f;
    root->Add(scroll);
    return root;
}

// ---------------- 界面设置 ----------------

void SettingsPage::BuildUi(const std::shared_ptr<State>& s, vcui::Stack& page) {
    auto& st = *ctx_->settings;
    auto self = this;

    auto theme = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"跟随系统", L"浅色", L"深色" },
        st.theme == L"light" ? 1 : (st.theme == L"dark" ? 2 : 0));
    theme->Changed = [self](int idx) {
        auto& cfg = *self->ctx_->settings;
        cfg.theme = idx == 0 ? L"auto" : (idx == 1 ? L"light" : L"dark");
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        if (self->ctx_->applyUi) self->ctx_->applyUi();
    };

    auto lang = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"简体中文", L"English" },
        st.language == L"en-US" ? 1 : 0);
    lang->Changed = [self](int idx) {
        auto& cfg = *self->ctx_->settings;
        cfg.language = idx == 1 ? L"en-US" : L"zh-CN";
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };

    Section(page, L"外观", { Row(L"主题", theme, 180.f), Row(L"语言", lang, 180.f) });

    // 强调色：六个色块按钮一行铺开
    auto colors = vcui::HBox(8.f);
    struct A { const wchar_t* name; const wchar_t* hex; };
    static const A palette[] = {
        { L"蓝", L"#0071EA" }, { L"青", L"#00B7C3" }, { L"绿", L"#107C10" },
        { L"橙", L"#D97706" }, { L"红", L"#D13438" }, { L"紫", L"#8764B8" },
    };
    for (const auto& a : palette) {
        auto btn = std::make_shared<vcui::Button>(std::wstring(a.name) + L" " + a.hex);
        btn->W = 110.f;
        auto hex = std::wstring(a.hex);
        btn->Clicked = [self, hex] {
            auto& cfg = *self->ctx_->settings;
            cfg.accent = hex;
            if (self->ctx_->saveSettings) self->ctx_->saveSettings();
            if (self->ctx_->applyUi) self->ctx_->applyUi();
            self->ctx_->Toast(L"强调色已切换为 " + hex, vcui::InfoTone::Success);
        };
        colors->Add(btn);
    }
    auto colorSp = vcui::Spacer(0.f);
    colorSp->Grow = 1.f;
    colors->Add(colorSp);
    auto colorRow = vcui::HBox(0.f);
    colorRow->PT = 10.f;
    colorRow->PB = 10.f;
    colorRow->Add(colors);
    Section(page, L"强调色", { colorRow });

    s->opacity = std::make_shared<vcui::Slider>((float)st.opacity);
    s->opacity->W = 300.f;
    s->opacityLabel = Caption(WFormat(L"%.0f%%", st.opacity * 100.0));
    s->opacityLabel->W = 52.f;
    auto opacityBox = vcui::HBox(8.f);
    opacityBox->Add(s->opacity);
    opacityBox->Add(s->opacityLabel);
    s->opacity->Changed = [self, s](float v) {
        auto& cfg = *self->ctx_->settings;
        cfg.opacity = v < .3f ? .3f : v;
        s->opacityLabel->Text = WFormat(L"%.0f%%", cfg.opacity * 100.0);
        if (self->ctx_->applyUi) self->ctx_->applyUi();
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };

    auto anim = std::make_shared<vcui::Switch>(st.animations);
    anim->Changed = [self](bool on) {
        auto& cfg = *self->ctx_->settings;
        cfg.animations = on;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };

    // 状态栏用强调色铺底，文字自动反色
    auto statusAccent = std::make_shared<vcui::Switch>(st.statusAccent);
    statusAccent->Changed = [self](bool on) {
        auto& cfg = *self->ctx_->settings;
        cfg.statusAccent = on;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        if (self->ctx_->applyUi) self->ctx_->applyUi();
    };

    Section(page, L"视觉与动效",
            { Row(L"界面不透明度", opacityBox, 360.f), Row(L"页面过渡动效", anim, 40.f),
              Row(L"状态栏使用强调色", statusAccent, 40.f) });
}

// ---------------- 全局设置 ----------------

void SettingsPage::BuildGlobal(const std::shared_ptr<State>& s, vcui::Stack& page) {
    auto& st = *ctx_->settings;
    auto self = this;

    auto mirror = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"BMCLAPI 镜像", L"Mojang 官方" },
        st.mirror == L"mojang" ? 1 : 0);
    mirror->Changed = [self](int idx) {
        auto& cfg = *self->ctx_->settings;
        cfg.mirror = idx == 0 ? L"bmclapi" : L"mojang";
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };

    auto conc = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"8", L"16", L"24", L"32" },
        st.concurrency <= 8 ? 0 : (st.concurrency <= 16 ? 1 : (st.concurrency <= 24 ? 2 : 3)));
    conc->Changed = [self](int idx) {
        static const int vals[] = { 8, 16, 24, 32 };
        auto& cfg = *self->ctx_->settings;
        cfg.concurrency = vals[idx < 0 || idx > 3 ? 1 : idx];
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        self->ctx_->Toast(L"并发数将在下次启动生效", vcui::InfoTone::Info);
    };

    // 单文件下载线程：大文件才分块并行，1~128 可选
    static const int kFileThreads[] = { 1, 2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 };
    std::vector<std::wstring> ftItems;
    int ftSel = 3;
    for (size_t i = 0; i < sizeof(kFileThreads) / sizeof(int); ++i) {
        ftItems.push_back(std::to_wstring(kFileThreads[i]));
        if (kFileThreads[i] == st.fileThreads) ftSel = (int)i;
    }
    auto ft = std::make_shared<vcui::Dropdown>(ftItems, ftSel);
    ft->Changed = [self](int idx) {
        static const int vals[] = { 1, 2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 };
        int n = (idx < 0 || idx > 11) ? 8 : vals[idx];
        auto& cfg = *self->ctx_->settings;
        cfg.fileThreads = n;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        self->ctx_->Toast(L"单文件下载线程改为 " + std::to_wstring(n) + L"，下次启动生效",
                          vcui::InfoTone::Info);
    };

    // 分块阈值：超过这个大小才拆块并发（128 KB ~ 8 MB）
    static const int kThresholdKb[] = { 128, 256, 512, 1024, 2048, 4096, 8192 };
    std::vector<std::wstring> thItems;
    int thSel = 3;
    for (size_t i = 0; i < sizeof(kThresholdKb) / sizeof(int); ++i) {
        int kb = kThresholdKb[i];
        thItems.push_back(kb < 1024 ? std::to_wstring(kb) + L" KB"
                                    : std::to_wstring(kb / 1024) + L" MB");
        if (kb == st.chunkThresholdKb) thSel = (int)i;
    }
    auto th = std::make_shared<vcui::Dropdown>(thItems, thSel);
    th->Changed = [self](int idx) {
        static const int vals[] = { 128, 256, 512, 1024, 2048, 4096, 8192 };
        int kb = (idx < 0 || idx > 6) ? 1024 : vals[idx];
        auto& cfg = *self->ctx_->settings;
        cfg.chunkThresholdKb = kb;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        std::wstring label = kb < 1024 ? std::to_wstring(kb) + L" KB"
                                       : std::to_wstring(kb / 1024) + L" MB";
        self->ctx_->Toast(L"分块阈值改为 " + label + L"，下次启动生效", vcui::InfoTone::Info);
    };

    s->cachePath = std::make_shared<vcui::Input>(L"留空则使用默认缓存目录");
    s->cachePath->Grow = 1.f;
    s->cachePath->Text = st.cacheDir.empty() ? AppCacheDir() : st.cacheDir;
    auto cpick = std::make_shared<vcui::Button>(L"选择");
    cpick->W = 80.f;
    auto crow = vcui::HBox(8.f);
    crow->Add(s->cachePath);
    crow->Add(cpick);
    cpick->Clicked = [self, s] {
        std::wstring dir;
        if (!PickFolder(dir)) return;
        s->cachePath->Text = dir;
        auto& cfg = *self->ctx_->settings;
        cfg.cacheDir = dir;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };
    s->cachePath->Changed = [self, s](const std::wstring& v) {
        auto& cfg = *self->ctx_->settings;
        cfg.cacheDir = Trim(v);
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };

    Section(page, L"下载",
            { Row(L"游戏下载源", mirror, 180.f), Row(L"同时下载任务数", conc, 140.f),
              Row(L"单文件下载线程", ft, 140.f), Row(L"分块阈值", th, 140.f),
              Row(L"缓存目录", crow, 520.f) });

    auto openCfg = std::make_shared<vcui::Button>(L"打开配置目录");
    openCfg->Clicked = [self] {
        std::wstring dir = AppConfigDir();
        EnsureDir(dir);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };
    auto saveAll = std::make_shared<vcui::Button>(L"立即保存", true);
    saveAll->Clicked = [self] {
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        self->ctx_->Toast(L"已保存到 global.json", vcui::InfoTone::Success);
    };
    auto footRow = vcui::HBox(8.f);
    footRow->MT = 16.f;
    auto fsp = vcui::Spacer(0.f);
    fsp->Grow = 1.f;
    footRow->Add(openCfg);
    footRow->Add(fsp);
    footRow->Add(saveAll);
    page.Add(footRow);
}

} // namespace vcl
