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

static const UiCtx* ctx_placeholder_ = nullptr;

namespace {

D2D1_COLOR_F Text2(const UiCtx& ctx) {
    return ctx.surface ? ctx.surface->Th().t.text2 : vcui::Hex(0x808080);
}
D2D1_COLOR_F Text3(const UiCtx& ctx) {
    return ctx.surface ? ctx.surface->Th().t.text3 : vcui::Hex(0x999999);
}

// 字段标题：在控件上方，小字、次要色
std::shared_ptr<vcui::Label> FieldTitle(const std::wstring& s) {
    auto l = std::make_shared<vcui::Label>(s, 13.f, 400);
    l->Color = ctx_placeholder_ ? Text2(*ctx_placeholder_) : vcui::Hex(0x606060);
    return l;
}

std::shared_ptr<vcui::Label> Hint(const std::wstring& s) {
    auto l = std::make_shared<vcui::Label>(s, 12.f, 400);
    l->Color = ctx_placeholder_ ? Text3(*ctx_placeholder_) : vcui::Hex(0x909090);
    l->Wrap = true;
    return l;
}

// 一个字段：标题 + 控件 + 可选说明，纵向排列，控件铺满这一列
std::shared_ptr<vcui::Widget> Field(const std::wstring& title, std::shared_ptr<vcui::Widget> ctl,
                                    const std::wstring& hint = std::wstring()) {
    auto col = vcui::VBox(6.f);
    col->W = -1.f;
    col->Add(FieldTitle(title));
    if (ctl) col->Add(ctl);
    if (!hint.empty()) col->Add(Hint(hint));
    return col;
}

// 两列并排（Bitflow 的 field-row）
std::shared_ptr<vcui::Widget> Pair(std::shared_ptr<vcui::Widget> a, std::shared_ptr<vcui::Widget> b) {
    auto row = vcui::HBox(12.f);
    row->W = -1.f;
    row->Cross = vcui::Stack::Start;
    a->Grow = 1.f;
    b->Grow = 1.f;
    row->Add(a);
    row->Add(b);
    return row;
}

// 勾选行：开关 + 说明文字，下方可跟一行小字
std::shared_ptr<vcui::Widget> CheckLine(const std::wstring& title, std::shared_ptr<vcui::Widget> sw,
                                        const std::wstring& hint = std::wstring()) {
    auto col = vcui::VBox(4.f);
    col->W = -1.f;
    auto line = vcui::HBox(10.f);
    line->Cross = vcui::Stack::Center;
    line->Add(sw);
    auto lb = std::make_shared<vcui::Label>(title, 13.f, 400);
    lb->Color = ctx_placeholder_ ? Text2(*ctx_placeholder_) : vcui::Hex(0x606060);
    lb->Grow = 1.f;
    line->Add(lb);
    col->Add(line);
    if (!hint.empty()) col->Add(Hint(hint));
    return col;
}

// 一张分组卡：标题在卡片内顶部，字段纵向堆叠
std::shared_ptr<vcui::Widget> GroupCard(const std::wstring& title,
                                        const std::vector<std::shared_ptr<vcui::Widget>>& fields) {
    auto card = std::make_shared<vcui::Card>();
    card->Shadow = false;
    card->Border = true;
    card->Radius = 2.f;
    card->Pad(0.f);

    auto inner = vcui::VBox(14.f);
    inner->PL = 18.f;
    inner->PR = 18.f;
    inner->PT = 14.f;
    inner->PB = 18.f;

    auto head = std::make_shared<vcui::Label>(title, 13.f, 400);
    head->Color = ctx_placeholder_ ? Text2(*ctx_placeholder_) : vcui::Hex(0x606060);
    inner->Add(head);
    for (auto& f : fields) inner->Add(f);
    card->Add(inner);
    return card;
}

} // namespace

std::shared_ptr<vcui::Widget> SettingsPage::Build(int sub) {
    ctx_placeholder_ = ctx_;
    auto page = vcui::VBox(12.f);
    auto s = std::make_shared<State>();
    s->sub = sub;

    if (sub == 0) {
        BuildUi(s, *page);
    } else {
        BuildGlobal(s, *page);
    }

    page->MB = 32.f;
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
    theme->W = -1.f;
    theme->Changed = [self](int idx) {
        auto& cfg = *self->ctx_->settings;
        cfg.theme = idx == 0 ? L"auto" : (idx == 1 ? L"light" : L"dark");
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        if (self->ctx_->applyUi) self->ctx_->applyUi();
    };

    auto lang = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"简体中文", L"English" },
        st.language == L"en-US" ? 1 : 0);
    lang->W = -1.f;
    lang->Changed = [self](int idx) {
        auto& cfg = *self->ctx_->settings;
        cfg.language = idx == 1 ? L"en-US" : L"zh-CN";
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };

    page.Add(GroupCard(L"外观", { Pair(Field(L"主题", theme),
                                      Field(L"语言", lang)) }));

    // 强调色：取色器 + 一组用得上强调色的示例
    D2D1_COLOR_F acc{};
    if (!vcui::ParseHex(st.accent, acc)) acc = vcui::Hex(0x0071EA);
    auto picker = std::make_shared<vcui::ColorPicker>(acc);
    picker->W = 320.f;
    picker->SquareH = 118.f;
    picker->Swatches = { 0x0071EA, 0x00B7C3, 0x107C10, 0xD97706, 0xD13438, 0x8764B8, 0xE3008C };
    picker->Changed = [self](D2D1_COLOR_F c) {
        auto& cfg = *self->ctx_->settings;
        std::wstring a = vcui::ToHex(c);
        if (cfg.accent == a) return;
        cfg.accent = a;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        if (self->ctx_->applyUi) self->ctx_->applyUi();
    };

    auto preview = vcui::HBox(12.f);
    preview->Cross = vcui::Stack::Center;
    auto pvBtn = std::make_shared<vcui::Button>(L"主色按钮", true);
    pvBtn->W = 112.f;
    auto pvSwitch = std::make_shared<vcui::Switch>(true);
    auto pvBadge = std::make_shared<vcui::Badge>(L"标签", vcui::Badge::Accent);
    preview->Add(pvBtn);
    preview->Add(pvSwitch);
    preview->Add(pvBadge);

    auto accentField = Field(L"自定义颜色", picker);
    // 示例摆在取色器右侧
    auto accentRow = vcui::HBox(24.f);
    accentRow->W = -1.f;
    accentRow->Cross = vcui::Stack::Start;
    accentField->Grow = 1.f;
    accentRow->Add(accentField);
    auto previewCol = vcui::VBox(6.f);
    previewCol->Add(FieldTitle(L"预览"));
    previewCol->Add(preview);
    accentRow->Add(previewCol);

    page.Add(GroupCard(L"强调色", { accentRow }));

    s->opacity = std::make_shared<vcui::Slider>((float)st.opacity);
    s->opacity->Grow = 1.f;
    s->opacityLabel = std::make_shared<vcui::Label>(WFormat(L"%.0f%%", st.opacity * 100.0), 12.f, 400);
    s->opacityLabel->W = 52.f;
    s->opacityLabel->Align = vcui::HAlign::Right;
    auto opacityBox = vcui::HBox(10.f);
    opacityBox->W = -1.f;
    opacityBox->Cross = vcui::Stack::Center;
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

    auto statusAccent = std::make_shared<vcui::Switch>(st.statusAccent);
    statusAccent->Changed = [self](bool on) {
        auto& cfg = *self->ctx_->settings;
        cfg.statusAccent = on;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        if (self->ctx_->applyUi) self->ctx_->applyUi();
    };

    auto zebra = std::make_shared<vcui::Switch>(st.listZebra);
    zebra->Changed = [self](bool on) {
        auto& cfg = *self->ctx_->settings;
        cfg.listZebra = on;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        if (self->ctx_->applyUi) self->ctx_->applyUi();
    };

    page.Add(GroupCard(L"视觉与动效",
                       { Field(L"界面不透明度", opacityBox),
                         CheckLine(L"页面过渡动效", anim),
                         CheckLine(L"状态栏使用强调色", statusAccent),
                         CheckLine(L"列表隔行浅底（斑马纹）", zebra) }));
}

// ---------------- 全局设置 ----------------

void SettingsPage::BuildGlobal(const std::shared_ptr<State>& s, vcui::Stack& page) {
    auto& st = *ctx_->settings;
    auto self = this;

    auto openCfg = std::make_shared<vcui::Button>(L"打开配置目录");
    openCfg->W = 132.f;
    openCfg->Clicked = [self] {
        std::wstring dir = AppConfigDir();
        EnsureDir(dir);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };
    auto saveAll = std::make_shared<vcui::Button>(L"立即保存", true);
    saveAll->W = 108.f;
    saveAll->Clicked = [self] {
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        self->ctx_->Toast(L"已保存到 global.json", vcui::InfoTone::Success);
    };

    auto actionBar = vcui::HBox(8.f);
    actionBar->W = -1.f;
    auto spacer = vcui::VBox(0.f);
    spacer->Grow = 1.f;
    actionBar->Add(spacer);
    actionBar->Add(openCfg);
    actionBar->Add(saveAll);
    page.Add(actionBar);

    auto mirror = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"BMCLAPI 镜像", L"Mojang 官方" },
        st.mirror == L"mojang" ? 1 : 0);
    mirror->W = -1.f;
    mirror->Changed = [self](int idx) {
        auto& cfg = *self->ctx_->settings;
        cfg.mirror = idx == 0 ? L"bmclapi" : L"mojang";
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        self->ctx_->Toast(L"下载源已切换，重新进下载页生效", vcui::InfoTone::Info);
    };

    auto conc = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"8", L"16", L"24", L"32" },
        st.concurrency <= 8 ? 0 : (st.concurrency <= 16 ? 1 : (st.concurrency <= 24 ? 2 : 3)));
    conc->W = -1.f;
    conc->Changed = [self](int idx) {
        static const int vals[] = { 8, 16, 24, 32 };
        auto& cfg = *self->ctx_->settings;
        cfg.concurrency = vals[idx < 0 || idx > 3 ? 1 : idx];
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        self->ctx_->Toast(L"并发数将在下次启动生效", vcui::InfoTone::Info);
    };

    static const int kFileThreads[] = { 1, 2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 };
    std::vector<std::wstring> ftItems;
    int ftSel = 3;
    for (size_t i = 0; i < sizeof(kFileThreads) / sizeof(int); ++i) {
        ftItems.push_back(std::to_wstring(kFileThreads[i]));
        if (kFileThreads[i] == st.fileThreads) ftSel = (int)i;
    }
    auto ft = std::make_shared<vcui::Dropdown>(ftItems, ftSel);
    ft->W = -1.f;
    ft->Changed = [self](int idx) {
        static const int vals[] = { 1, 2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 };
        int n = (idx < 0 || idx > 11) ? 8 : vals[idx];
        auto& cfg = *self->ctx_->settings;
        cfg.fileThreads = n;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        self->ctx_->Toast(L"单文件下载线程改为 " + std::to_wstring(n) + L"，下次启动生效",
                          vcui::InfoTone::Info);
    };

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
    th->W = -1.f;
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
    cpick->W = 76.f;
    auto crow = vcui::HBox(8.f);
    crow->W = -1.f;
    crow->Cross = vcui::Stack::Center;
    crow->Add(s->cachePath);
    crow->Add(cpick);
    cpick->Clicked = [self, s] {
        std::wstring dir;
        HWND hw = self->ctx_->surface ? self->ctx_->surface->Hwnd() : nullptr;
        if (!PickFolder(dir, hw)) return;
        s->cachePath->Text = dir;
        auto& cfg = *self->ctx_->settings;
        cfg.cacheDir = dir;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        if (self->ctx_->surface) self->ctx_->surface->Invalidate();
    };
    s->cachePath->Changed = [self, s](const std::wstring& v) {
        auto& cfg = *self->ctx_->settings;
        cfg.cacheDir = Trim(v);
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };

    page.Add(GroupCard(L"下载", {
        Pair(Field(L"游戏下载源", mirror),
             Field(L"同时下载任务数", conc)),
        Pair(Field(L"单文件下载线程", ft),
             Field(L"分块阈值", th)),
        Field(L"缓存目录", crow),
    }));
}

} // namespace vcl
