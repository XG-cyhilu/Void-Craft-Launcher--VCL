#include "ui/SettingsPage.h"

#include <windows.h>
#include <shellapi.h>

#include "core/Paths.h"
#include "core/Text.h"

namespace vcl {

struct SettingsPage::State {
    std::shared_ptr<vcui::Stack> rootList;
    std::shared_ptr<vcui::Label> hint;
    std::shared_ptr<vcui::Input> rootPath;
    std::shared_ptr<vcui::Input> javaPath;
    std::shared_ptr<vcui::Input> cachePath;
    std::shared_ptr<vcui::Slider> opacity;
    std::shared_ptr<vcui::Label> opacityLabel;
    int sub = 0;
};

std::vector<std::wstring> SettingsPage::SubNav() {
    return { L"界面设置", L"全局设置" };
}

SettingsPage::SettingsPage(UiCtx* ctx) : ctx_(ctx) {}

static std::shared_ptr<vcui::Card> Group(const std::wstring& title) {
    auto card = std::make_shared<vcui::Card>();
    auto col = vcui::VBox(10.f);
    col->Pad(18.f);
    col->Add(Text(title, vcui::Type::Subtitle2));
    card->Add(col);
    card->W = -1.f;
    card->Id = L"card";
    return card;
}

static void GroupAdd(vcui::Card& card, std::shared_ptr<vcui::Widget> w) {
    auto c = card.Children().front();
    c->Add(std::move(w));
}

// 一行：左标题 + 右控件（控件固定宽度）
static std::shared_ptr<vcui::Stack> SettingRow(const std::wstring& title,
                                               const std::wstring& sub,
                                               std::shared_ptr<vcui::Widget> ctl,
                                               float ctlW = 200.f) {
    auto row = vcui::HBox(12.f);
    auto col = vcui::VBox(2.f);
    col->Grow = 1.f;
    col->Add(Text(title));
    if (!sub.empty()) col->Add(Caption(sub));
    row->Add(col);
    if (ctl) {
        ctl->W = ctlW;
        row->Add(ctl);
    }
    return row;
}

static std::wstring Slice(const std::wstring& in) {
    std::wstring t = Trim(in);
    if (t.size() > 2 && t.front() == L'"' && t.back() == L'"') t = t.substr(1, t.size() - 2);
    return t;
}

std::shared_ptr<vcui::Widget> SettingsPage::Build(int sub) {
    auto page = vcui::VBox(12.f);
    auto s = std::make_shared<State>();
    s->sub = sub;

    if (sub == 0) {
        BuildUi(s, *page);
    } else {
        BuildGlobal(s, *page);
    }

    // 设置项比一屏高，整页套一层滚动容器（内容按自身高度排）
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

    // 主题
    auto theme = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"跟随系统", L"浅色", L"深色" },
        st.theme == L"light" ? 1 : (st.theme == L"dark" ? 2 : 0));
    theme->Changed = [self, theme](int idx) {
        auto& cfg = *self->ctx_->settings;
        cfg.theme = idx == 0 ? L"auto" : (idx == 1 ? L"light" : L"dark");
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        if (self->ctx_->applyUi) self->ctx_->applyUi();
    };

    auto lang = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"简体中文", L"English" },
        st.language == L"en-US" ? 1 : 0);
    lang->Changed = [self, lang](int idx) {
        auto& cfg = *self->ctx_->settings;
        cfg.language = idx == 1 ? L"en-US" : L"zh-CN";
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };

    auto themeCard = Group(L"外观");
    GroupAdd(*themeCard, SettingRow(L"主题", L"跟随系统时读 Windows 的应用模式", theme, 180.f));
    GroupAdd(*themeCard, SettingRow(L"语言", L"界面语言（v0.1 仅记录偏好）", lang, 180.f));
    page.Add(themeCard);

    // 强调色
    auto accentCard = Group(L"强调色");
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
    GroupAdd(*accentCard, colors);
    page.Add(accentCard);

    // 不透明度 + 动效
    auto behaviorCard = Group(L"视觉与动效");
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
    GroupAdd(*behaviorCard, SettingRow(L"界面不透明度", L"作用于窗口内容层（v0.1 仅背景层）", opacityBox, 360.f));

    auto anim = std::make_shared<vcui::Switch>(st.animations);
    anim->Changed = [self](bool on) {
        auto& cfg = *self->ctx_->settings;
        cfg.animations = on;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };
    GroupAdd(*behaviorCard, SettingRow(L"页面过渡动效", L"切页淡入与错峰入场", anim, 40.f));
    page.Add(behaviorCard);

    page.Add(Caption(L"提示：全局设置中的下载源与并发数会影响下载页的行为。"));
}

// ---------------- 全局设置 ----------------

void SettingsPage::BuildGlobal(const std::shared_ptr<State>& s, vcui::Stack& page) {
    auto& st = *ctx_->settings;
    auto self = this;

    // 游戏目录
    auto rootCard = Group(L"游戏目录");
    s->rootPath = std::make_shared<vcui::Input>(L"例如 %APPDATA%\\.minecraft");
    s->rootPath->Grow = 1.f;
    s->rootPath->Text = CurrentRootPath(st);

    auto pick = std::make_shared<vcui::Button>(L"浏览");
    pick->W = 72.f;
    auto apply = std::make_shared<vcui::Button>(L"应用", true);
    apply->W = 72.f;

    auto rootRow = vcui::HBox(8.f);
    rootRow->Add(s->rootPath);
    rootRow->Add(pick);
    rootRow->Add(apply);
    GroupAdd(*rootCard, rootRow);
    GroupAdd(*rootCard, Caption(L"下载的模组/资源包等会落到该目录下对应的子文件夹"));

    s->rootList = vcui::VBox(0.f);
    GroupAdd(*rootCard, s->rootList);
    auto addRoot = std::make_shared<vcui::Button>(L"添加为目录列表中的一项");
    addRoot->W = 220.f;
    GroupAdd(*rootCard, addRoot);
    page.Add(rootCard);

    pick->Clicked = [self, s] {
        std::wstring dir;
        if (!PickFolder(dir)) return;
        s->rootPath->Text = dir;
    };
    apply->Clicked = [self, s] {
        std::wstring p = NormalizePath(Slice(s->rootPath->Text));
        if (p.empty()) {
            self->ctx_->Toast(L"路径为空", vcui::InfoTone::Warning);
            return;
        }
        EnsureDir(p);
        auto& cfg = *self->ctx_->settings;
        cfg.roots.push_back({ p, p });
        cfg.currentRoot = (int)cfg.roots.size() - 1;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        self->RenderRoots(s);
        self->ctx_->Toast(L"已切换游戏目录", vcui::InfoTone::Success);
    };
    addRoot->Clicked = [self, s] {
        std::wstring p = NormalizePath(Slice(s->rootPath->Text));
        if (p.empty()) return;
        EnsureDir(p);
        auto& cfg = *self->ctx_->settings;
        cfg.roots.push_back({ p, p });
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        self->RenderRoots(s);
    };

    RenderRoots(s);

    // 下载
    auto dlCard = Group(L"下载");
    auto mirror = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"BMCLAPI 镜像", L"Mojang 官方" },
        st.mirror == L"mojang" ? 1 : 0);
    mirror->Changed = [self](int idx) {
        auto& cfg = *self->ctx_->settings;
        cfg.mirror = idx == 0 ? L"bmclapi" : L"mojang";
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };
    GroupAdd(*dlCard, SettingRow(L"游戏下载源", L"只影响「游戏」子页；其余五类固定走 Modrinth",
                                 mirror, 180.f));

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
    GroupAdd(*dlCard, SettingRow(L"同时下载任务数", L"小文件并发；大文件另走分块路径", conc, 140.f));
    page.Add(dlCard);

    // Java 与缓存
    auto javaCard = Group(L"Java 与缓存");
    s->javaPath = std::make_shared<vcui::Input>(L"留空则自动检测");
    s->javaPath->Grow = 1.f;
    s->javaPath->Text = st.javaPath;
    auto jpick = std::make_shared<vcui::Button>(L"选择");
    jpick->W = 80.f;
    auto jrow = vcui::HBox(8.f);
    jrow->Add(s->javaPath);
    jrow->Add(jpick);
    GroupAdd(*javaCard, SettingRow(L"Java 可执行文件", L"javaw.exe 的完整路径", jrow, 520.f));
    jpick->Clicked = [self, s] {
        std::wstring dir;
        if (!PickFolder(dir)) return;
        s->javaPath->Text = dir + L"\\javaw.exe";
    };
    s->javaPath->Changed = [self, s](const std::wstring& v) {
        auto& cfg = *self->ctx_->settings;
        cfg.javaPath = Trim(v);
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
    };

    s->cachePath = std::make_shared<vcui::Input>(L"留空则使用默认缓存目录");
    s->cachePath->Grow = 1.f;
    s->cachePath->Text = st.cacheDir.empty() ? AppCacheDir() : st.cacheDir;
    auto cpick = std::make_shared<vcui::Button>(L"选择");
    cpick->W = 80.f;
    auto crow = vcui::HBox(8.f);
    crow->Add(s->cachePath);
    crow->Add(cpick);
    GroupAdd(*javaCard, SettingRow(L"缓存目录", L"下载临时文件与索引", crow, 520.f));
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
    page.Add(javaCard);

    auto footRow = vcui::HBox(8.f);
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
    auto fsp = vcui::Spacer(0.f);
    fsp->Grow = 1.f;
    footRow->Add(openCfg);
    footRow->Add(fsp);
    footRow->Add(saveAll);
    page.Add(footRow);
}

void SettingsPage::RenderRoots(const std::shared_ptr<State>& s) {
    if (!s->rootList) return;
    s->rootList->Clear();
    auto& cfg = *ctx_->settings;
    if (cfg.roots.empty()) {
        s->rootList->Add(Caption(L"还没有配置游戏目录，当前使用系统默认 .minecraft"));
        return;
    }
    auto self = this;
    for (size_t i = 0; i < cfg.roots.size(); ++i) {
        auto row = vcui::HBox(10.f);
        row->Pad(8.f);
        auto col = vcui::VBox(2.f);
        col->Grow = 1.f;
        col->Add(Text(cfg.roots[i].second.empty() ? cfg.roots[i].first : cfg.roots[i].second,
                      vcui::Type::Body1Strong));
        col->Add(Caption(cfg.roots[i].first));
        row->Add(col);

        if ((int)i == cfg.currentRoot) {
            auto b = std::make_shared<vcui::Badge>(L"当前", vcui::Badge::Accent);
            b->W = 56.f;
            row->Add(b);
        } else {
            auto use = std::make_shared<vcui::Button>(L"设为当前");
            use->W = 92.f;
            use->Clicked = [self, s, i] {
                auto& c = *self->ctx_->settings;
                c.currentRoot = (int)i;
                if (self->ctx_->saveSettings) self->ctx_->saveSettings();
                self->RenderRoots(s);
            };
            row->Add(use);
        }
        auto del = std::make_shared<vcui::Button>(L"移除");
        del->W = 72.f;
        del->Danger = true;
        del->Clicked = [self, s, i] {
            auto& c = *self->ctx_->settings;
            if (i < c.roots.size()) c.roots.erase(c.roots.begin() + (long)i);
            if (c.currentRoot >= (int)c.roots.size()) c.currentRoot = 0;
            if (self->ctx_->saveSettings) self->ctx_->saveSettings();
            self->RenderRoots(s);
        };
        row->Add(del);

        s->rootList->Add(row);
        s->rootList->Add(std::make_shared<vcui::Divider>());
    }
}

} // namespace vcl


