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

// Row 里要取副标题颜色，但自由函数拿不到 ctx；用一个构建期指针把当前 UiCtx 递进去
static const UiCtx* ctx_placeholder_ = nullptr;

namespace {

// 设置列宽：整窗 1200 宽时把人眼要扫的"标题 → 控件"跨度收窄，行不会拉成一条长表格线
const float kColW = 720.f;

// 1.2px 细分隔线：库里的 Divider 固定 1px，设置页的行距靠它切分
class ThinRule : public vcui::Widget {
public:
    float Thickness = 1.2f;
    ThinRule() { H = 1.2f; W = kColW; }
    D2D1_SIZE_F Desired(float availW, float) override {
        return D2D1::SizeF(W > 0 ? W : availW, Thickness);
    }
    void Paint(vcui::PaintCtx& ctx) override {
        ctx.g.Line(fr_.left, fr_.top + .5f, fr_.right, fr_.top + .5f, ctx.t.divider, Thickness);
    }
};

D2D1_COLOR_F Text2(const UiCtx& ctx) {
    return ctx.surface ? ctx.surface->Th().t.text2 : vcui::Hex(0x808080);
}

// 一行：左「标题 + 可选副标题」，右控件。行铺满所在卡片的宽度。
std::shared_ptr<vcui::Widget> Row(const std::wstring& title, std::shared_ptr<vcui::Widget> ctl,
                                  float ctlW = 200.f, const std::wstring& sub = std::wstring(),
                                  vcui::VAlign align = vcui::VAlign::Middle) {
    auto row = vcui::HBox(16.f);
    row->W = -1.f;
    row->Cross = align == vcui::VAlign::Top ? vcui::Stack::Start : vcui::Stack::Center;
    row->PT = align == vcui::VAlign::Top ? 12.f : 11.f;
    row->PB = 11.f;

    auto col = vcui::VBox(3.f);
    col->Grow = 1.f;
    auto name = Text(title, vcui::Type::Body1Strong);
    col->Add(name);
    if (!sub.empty()) {
        auto hint = Caption(sub);
        if (ctx_placeholder_) hint->Color = Text2(*ctx_placeholder_);
        col->Add(hint);
    }
    row->Add(col);

    if (ctl) {
        ctl->W = ctlW;
        row->Add(ctl);
    }
    return row;
}

// 一节：小节标题 + 一张卡片（圆角 + 细描边，行间用 1.2px 细线切开，左右各留 16 内边距）
void Section(vcui::Stack& page, const std::wstring& title,
             const std::vector<std::shared_ptr<vcui::Widget>>& rows) {
    auto head = Text(title, vcui::Type::Subtitle2);
    head->W = kColW;
    head->MT = 26.f;
    head->MB = 8.f;
    page.Add(head);

    auto card = std::make_shared<vcui::Card>();
    card->Shadow = false;
    card->Radius = 8.f;
    card->Pad(0.f);
    card->W = kColW;

    auto inner = vcui::VBox(0.f);
    inner->PL = 16.f;
    inner->PR = 16.f;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) inner->Add(std::make_shared<ThinRule>());
        inner->Add(rows[i]);
    }
    card->Add(inner);
    page.Add(card);
}

} // namespace

std::shared_ptr<vcui::Widget> SettingsPage::Build(int sub) {
    ctx_placeholder_ = ctx_;
    auto page = vcui::VBox(0.f);
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

    Section(page, L"外观",
            { Row(L"主题", theme, 180.f, L"浅色 / 深色 / 跟随系统"),
              Row(L"语言", lang, 180.f) });

    // 强调色：右列放取色器，和其它行的控件右缘对齐
    D2D1_COLOR_F acc{};
    if (!vcui::ParseHex(st.accent, acc)) acc = vcui::Hex(0x0071EA);
    auto picker = std::make_shared<vcui::ColorPicker>(acc);
    picker->W = 320.f;
    picker->SquareH = 118.f;
    picker->Swatches = { 0x0071EA, 0x00B7C3, 0x107C10, 0xD97706, 0xD13438, 0x8764B8, 0xE3008C };
    picker->Changed = [self](D2D1_COLOR_F c) {
        auto& cfg = *self->ctx_->settings;
        std::wstring a = vcui::ToHex(c);          // "#RRGGBB"
        if (cfg.accent == a) return;
        cfg.accent = a;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        if (self->ctx_->applyUi) self->ctx_->applyUi();   // 拖动时实时预览
    };
    // 强调色这一行左边顺手放一组"用得上强调色"的示例，既不空也不只是装饰
    auto accentRow = vcui::HBox(24.f);
    accentRow->W = -1.f;
    accentRow->Cross = vcui::Stack::Center;   // 左右两列等高居中，别把空白全堆在左下角
    accentRow->PT = 12.f;
    accentRow->PB = 12.f;

    auto left = vcui::VBox(3.f);
    left->Grow = 1.f;
    left->Add(Text(L"自定义颜色", vcui::Type::Body1Strong));
    auto hint = Caption(L"按钮、选中态、状态栏都跟着它走");
    hint->Color = Text2(*ctx_);
    left->Add(hint);

    auto preview = vcui::HBox(12.f);
    preview->Cross = vcui::Stack::Center;
    preview->MT = 16.f;
    auto pvBtn = std::make_shared<vcui::Button>(L"主色按钮", true);
    pvBtn->W = 112.f;
    auto pvSwitch = std::make_shared<vcui::Switch>(true);
    auto pvBadge = std::make_shared<vcui::Badge>(L"标签", vcui::Badge::Accent);
    preview->Add(pvBtn);
    preview->Add(pvSwitch);
    preview->Add(pvBadge);
    left->Add(preview);
    accentRow->Add(left);

    picker->W = 320.f;
    accentRow->Add(picker);
    Section(page, L"强调色", { accentRow });

    s->opacity = std::make_shared<vcui::Slider>((float)st.opacity);
    s->opacity->W = 300.f;
    s->opacityLabel = Caption(WFormat(L"%.0f%%", st.opacity * 100.0));
    s->opacityLabel->W = 52.f;
    auto opacityBox = vcui::HBox(8.f);
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

    // 状态栏用强调色铺底，文字自动反色
    auto statusAccent = std::make_shared<vcui::Switch>(st.statusAccent);
    statusAccent->Changed = [self](bool on) {
        auto& cfg = *self->ctx_->settings;
        cfg.statusAccent = on;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        if (self->ctx_->applyUi) self->ctx_->applyUi();
    };

    Section(page, L"视觉与动效",
            { Row(L"界面不透明度", opacityBox, 360.f, L"最低 30%，只影响内容区与侧栏"),
              Row(L"页面过渡动效", anim, 40.f, L"切页时的淡入与错峰入场"),
              Row(L"状态栏使用强调色", statusAccent, 40.f, L"状态栏底色换成强调色，文字自动反色") });
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
        self->ctx_->Toast(L"下载源已切换，重新进下载页生效", vcui::InfoTone::Info);
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
    crow->Cross = vcui::Stack::Center;
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
            { Row(L"游戏下载源", mirror, 180.f, L"版本清单与 jar 的来源，失败会自动回退官方"),
              Row(L"同时下载任务数", conc, 140.f, L"同时跑几个任务，多了抢带宽"),
              Row(L"单文件下载线程", ft, 140.f, L"大文件分块并发，1~128，下次启动生效"),
              Row(L"分块阈值", th, 140.f, L"超过这个大小才拆块，128 KB ~ 8 MB，下次启动生效"),
              Row(L"缓存目录", crow, 460.f, L"图标 / 会话 JSON 的落盘位置，关启动器即清") });

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
    footRow->W = kColW;
    footRow->MT = 20.f;
    auto fsp = vcui::Spacer(0.f);
    fsp->Grow = 1.f;
    footRow->Add(openCfg);
    footRow->Add(fsp);
    footRow->Add(saveAll);
    page.Add(footRow);
}

} // namespace vcl
