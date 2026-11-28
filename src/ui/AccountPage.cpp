#include "ui/AccountPage.h"
#include "ui/styles/token.h"

#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

#include <algorithm>
#include <thread>

#include "core/Accounts.h"
#include "ui/styles/token.h"
#include "core/LittleSkin.h"
#include "ui/styles/token.h"
#include "core/Text.h"
#include "ui/styles/token.h"

namespace vcl {

namespace {

D2D1_COLOR_F T2(const UiCtx& c) { return c.surface ? c.surface->Th().t.text2 : vcui::Hex(tok::Text2); }
D2D1_COLOR_F T3(const UiCtx& c) { return c.surface ? c.surface->Th().t.text3 : vcui::Hex(tok::Text3); }
D2D1_COLOR_F ErrColor() { return vcui::Hex(tok::Danger); }

std::shared_ptr<vcui::Label> Lbl(const std::wstring& s, float size, D2D1_COLOR_F col, float w = -1.f,
                                 bool ellipsis = false) {
    auto l = std::make_shared<vcui::Label>(s, size, 400);
    l->Color = col;
    l->Ellipsis = ellipsis;
    if (w > 0) l->W = w;
    return l;
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

std::wstring NewGuid() {
    GUID g{};
    if (FAILED(CoCreateGuid(&g))) return L"vcl-client-token";
    wchar_t s[64]{};
    StringFromGUID2(g, s, 64);
    std::wstring out = s;
    if (!out.empty() && out.front() == L'{') out.erase(0, 1);
    if (!out.empty() && out.back() == L'}') out.pop_back();
    return out;
}

// JSON 字符串转义（账号里可能有引号/反斜杠；邮箱一般用不到，但别赌）
std::string JsonEscape(const std::wstring& s) {
    std::string u = WideToUtf8(s);
    std::string out;
    for (char c : u) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c);
        }
    }
    return out;
}

// 头像：强调色圆底 + 首字母
class Avatar : public vcui::Widget {
public:
    std::wstring Name;
    float Box = 40.f;
    Avatar() {
        W = Box;
        H = Box;
    }
    D2D1_SIZE_F Desired(float, float) override { return D2D1::SizeF(Box, Box); }
    void Paint(vcui::PaintCtx& ctx) override {
        D2D1_RECT_F fr = Frame();
        float r = (fr.bottom - fr.top) * .5f;
        ctx.g.RoundRect(fr, r, vcui::A(ctx.t.accent, ctx.t.dark ? .30f : .14f));
        ctx.g.StrokeRect(fr, r, vcui::A(ctx.t.accent, .34f), ctx.Hair());
        if (Name.empty()) return;
        wchar_t ch = (wchar_t)towupper(Name[0]);
        ctx.g.Text(fr, std::wstring(1, ch), 16.f, 600, ctx.t.accent, vcui::HAlign::Center,
                   vcui::VAlign::Middle);
    }
};

// 列表行：整行可点（点击切换当前账户），悬停给一层很浅的底
class AccountRow : public vcui::Stack {
public:
    bool Selected = false;
    AccountRow() : vcui::Stack(vcui::Stack::Row, 12.f) {
        H = 62.f;
        Interactive = true;
        Cross = vcui::Stack::Center;
        PL = 16.f;
        PR = 12.f;
    }
    void Paint(vcui::PaintCtx& ctx) override {
        D2D1_RECT_F fr = Frame();
        float h = HoverA();
        if (Selected)
            ctx.g.RoundRect(fr, tok::RadiusCard, vcui::A(ctx.t.accent, ctx.t.dark ? .12f : .07f));
        else if (h > .003f)
            ctx.g.RoundRect(fr, tok::RadiusCard, vcui::A(ctx.t.xt, .05f * h));
    }
};

// 登录方式一栏：标题 + 说明 + 右侧箭头，整行可点
class MethodTile : public vcui::Widget {
public:
    std::wstring Title;
    std::wstring Desc;
    bool Muted = false;

    MethodTile(std::wstring title, std::wstring desc, bool muted = false)
        : Title(std::move(title)), Desc(std::move(desc)), Muted(muted) {
        H = 66.f;
        Interactive = !muted;
        Enabled = !muted;
    }

    D2D1_SIZE_F Desired(float availW, float) override {
        return D2D1::SizeF(availW > 0 ? availW : 0.f, H);
    }

    void Paint(vcui::PaintCtx& ctx) override {
        D2D1_RECT_F fr = Frame();
        float h = HoverA();
        D2D1_COLOR_F bg = Muted ? vcui::A(ctx.t.xt, ctx.t.dark ? .04f : .025f)
                               : vcui::A(ctx.t.xt, (ctx.t.dark ? .05f : .03f) + .05f * h);
        ctx.g.RoundRect(fr, tok::RadiusCard, bg);
        ctx.g.StrokeRect(fr, tok::RadiusCard, vcui::A(ctx.t.xt, Muted ? .08f : .12f), ctx.Hair());

        D2D1_COLOR_F ink = Muted ? ctx.t.text3 : ctx.t.wz;
        D2D1_RECT_F tr{ fr.left + 14.f, fr.top + 11.f, fr.right - 34.f, fr.top + 34.f };
        ctx.g.Text(tr, Title, 14.f, 600, ink, vcui::HAlign::Left, vcui::VAlign::Middle);

        D2D1_RECT_F dr{ fr.left + 14.f, fr.top + 34.f, fr.right - 34.f, fr.bottom - 8.f };
        ctx.g.Text(dr, Desc, 11.f, 400, Muted ? ctx.t.text3 : ctx.t.text2, vcui::HAlign::Left,
                   vcui::VAlign::Middle);

        D2D1_RECT_F cr{ fr.right - 30.f, fr.top, fr.right - 10.f, fr.bottom };
        ctx.g.Text(cr, L"\x203A", 16.f, 400, Muted ? ctx.t.text3 : ctx.t.text2,
                   vcui::HAlign::Center, vcui::VAlign::Middle);
    }
};

} // namespace

AccountPage::AccountPage(UiCtx* ctx) : ctx_(ctx) {}

std::vector<std::wstring> AccountPage::SubNav() { return { L"账户" }; }

std::shared_ptr<vcui::Widget> AccountPage::Build(int sub) {
    (void)sub;
    return BuildPage();
}

std::shared_ptr<vcui::Widget> AccountPage::BuildPage() {
    auto page = Col(0.f);
    page->Grow = 1.f;

    // 账户列表卡片（卡内不再套卡：行用底色与分割线分层）
    auto card = std::make_shared<vcui::Card>();
    card->Shadow = false;
    card->Border = true;
    card->Radius = tok::RadiusCard;
    card->Pad(0.f);

    auto inner = vcui::VBox(0.f);

    auto bar = vcui::HBox(8.f);
    bar->W = -1.f;
    bar->Cross = vcui::Stack::Center;
    bar->PT = 11.f;
    bar->PB = 10.f;
    bar->PL = 16.f;
    bar->PR = 12.f;
    bar->Add(Lbl(L"账户列表", 13.f, T2(*ctx_)));
    count_ = std::make_shared<vcui::Badge>(L"0", vcui::Badge::Neutral);
    bar->Add(count_);
    auto sp2 = std::make_shared<vcui::Widget>();
    sp2->Grow = 1.f;
    bar->Add(sp2);

    auto addBtn = std::make_shared<vcui::Button>(L"添加账户", true);
    addBtn->W = 92.f;
    bar->Add(addBtn);
    inner->Add(bar);
    inner->Add(std::make_shared<vcui::Divider>());

    list_ = vcui::VBox(0.f);
    list_->W = -1.f;
    inner->Add(list_);

    hint_ = Lbl(L"还没有账户，点「添加账户」开始。", 12.f, T3(*ctx_));
    hint_->PT = 16.f;
    hint_->PB = 16.f;
    hint_->PL = 16.f;
    hint_->Visible = false;
    inner->Add(hint_);

    card->Add(inner);
    page->Add(card);

    auto self = this;
    addBtn->Clicked = [self] { self->OpenLogin(); };
    FillList();
    return page;
}

void AccountPage::FillList() {
    if (!list_ || !ctx_ || !ctx_->settings) return;
    auto& cfg = *ctx_->settings;
    list_->Clear();
    if (count_) count_->Text = std::to_wstring(cfg.accounts.size());
    if (hint_) hint_->Visible = cfg.accounts.empty();

    auto self = this;
    for (size_t i = 0; i < cfg.accounts.size(); ++i) {
        const AccountEntry& a = cfg.accounts[i];
        const bool cur = (int)i == cfg.currentAccount;

        auto row = std::make_shared<AccountRow>();
        row->W = -1.f;
        row->Selected = cur;

        auto av = std::make_shared<Avatar>();
        av->Name = a.name;
        row->Add(av);

        auto info = vcui::VBox(3.f);
        info->Grow = 1.f;
        auto nameRow = vcui::HBox(8.f);
        nameRow->Cross = vcui::Stack::Center;
        nameRow->Add(std::make_shared<vcui::Label>(a.name, 14.f, 600));
        if (a.type == L"littleskin")
            nameRow->Add(std::make_shared<vcui::Badge>(L"LittleSkin", vcui::Badge::Accent));
        else
            nameRow->Add(std::make_shared<vcui::Badge>(L"离线", vcui::Badge::Warning));
        if (cur) nameRow->Add(std::make_shared<vcui::Badge>(L"当前", vcui::Badge::Success));
        info->Add(nameRow);

        const std::wstring shownUuid = a.type == L"littleskin" ? DashUuid(a.uuid) : a.uuid;
        auto uuidLbl = Lbl(shownUuid, 11.f, T3(*ctx_), -1.f, true);
        uuidLbl->Tooltip = shownUuid;
        info->Add(uuidLbl);
        row->Add(info);

        if (!cur) {
            auto use = std::make_shared<vcui::Button>(L"设为当前");
            use->W = 92.f;
            use->Clicked = [self, i] {
                SetCurrentAccount(*self->ctx_->settings, (int)i);
                self->NotifyChanged(L"已切换到 " + self->ctx_->settings->accounts[i].name);
            };
            row->Add(use);
        } else {
            auto copy = std::make_shared<vcui::Button>(L"复制 UUID");
            copy->W = 92.f;
            copy->Clicked = [self, shownUuid] {
                CopyText(shownUuid);
                self->ctx_->Toast(L"UUID 已复制到剪贴板", vcui::InfoTone::Success);
            };
            row->Add(copy);
        }

        auto del = std::make_shared<vcui::Button>(L"移除");
        del->Danger = true;
        del->W = 64.f;
        del->Clicked = [self, i] {
            auto& c = *self->ctx_->settings;
            if (i >= c.accounts.size()) return;
            const std::wstring who = c.accounts[i].name;
            RemoveAccount(c, (int)i);
            self->NotifyChanged(L"已移除账户 " + who);
        };
        row->Add(del);

        // 空白处点击 = 切换当前；行内按钮自己接管点击
        row->Clicked = [self, i] {
            if ((int)i == self->ctx_->settings->currentAccount) return;
            SetCurrentAccount(*self->ctx_->settings, (int)i);
            self->NotifyChanged(L"已切换到 " + self->ctx_->settings->accounts[i].name);
        };

        list_->Add(row);
        list_->Add(std::make_shared<vcui::Divider>());
    }
}

void AccountPage::NotifyChanged(const std::wstring& toast) {
    if (ctx_->saveSettings) ctx_->saveSettings();
    if (ctx_->applyUi) ctx_->applyUi();     // 状态栏账户名
    FillList();
    if (ctx_->surface) ctx_->surface->Invalidate();
    if (!toast.empty()) ctx_->Toast(toast, vcui::InfoTone::Success);
}

// ---------------- 登录弹窗 ----------------

void AccountPage::OpenLogin() {
    if (!ctx_ || !ctx_->surface) return;
    if (panelBody_) return;     // 弹窗已经开着

    auto card = OpenPanel(ctx_->surface, L"登录", 520.f, 436.f,
                          [this] {
                              // 用户点遮罩 / 关闭按钮 / Esc：OpenPanel 已经移除了覆盖层，这里只清状态
                              oauth_.reset();
                              panelStatus_.reset();
                              statusOff_.reset();
                              statusSecret_.reset();
                              statusOAuth_.reset();
                              panelRoot_ = nullptr;
                              panelBody_.reset();
                              stepMethods_.reset();
                              stepOffline_.reset();
                              stepSecret_.reset();
                              stepOAuth_.reset();
                          },
                          &panelBody_);
    if (!card || !panelBody_) return;

    vcui::Widget* root = card.get();
    while (root && root->Parent) root = root->Parent;
    panelRoot_ = root;

    panelBody_->Clear();
    BuildSteps();
    ShowStep(0);
    if (ctx_->surface) ctx_->surface->Invalidate();
}

void AccountPage::BuildSteps() {
    stepMethods_ = vcui::VBox(6.f);
    stepMethods_->W = -1.f;
    stepOffline_ = vcui::VBox(8.f);
    stepOffline_->W = -1.f;
    stepSecret_ = vcui::VBox(8.f);
    stepSecret_->W = -1.f;
    stepOAuth_ = vcui::VBox(8.f);
    stepOAuth_->W = -1.f;

    panelBody_->Add(stepMethods_);
    panelBody_->Add(stepOffline_);
    panelBody_->Add(stepSecret_);
    panelBody_->Add(stepOAuth_);

    BuildMethodsStep();
    BuildOfflineStep();
    BuildSecretStep();
    BuildOAuthStep();
}

void AccountPage::BuildMethodsStep() {
    auto self = this;
    stepMethods_->Clear();

    auto offline = std::make_shared<MethodTile>(L"离线登录", L"直接输入玩家名，不联网；UUID 按 Java 离线规则生成。");
    offline->Clicked = [self] { self->ShowStep(1); };
    stepMethods_->Add(offline);

    auto lsTile = std::make_shared<MethodTile>(
        L"LittleSkin 登录", L"用皮肤站账号登录，可选密码或 OAuth2 浏览器授权。");
    lsTile->Clicked = [self] { self->ShowStep(2); };
    stepMethods_->Add(lsTile);

    auto ms = std::make_shared<MethodTile>(L"Microsoft 登录", L"正版账户还没接，先留个位置。", true);
    ms->Tooltip = L"未接入";
    stepMethods_->Add(ms);
}

void AccountPage::BuildOfflineStep() {
    auto self = this;
    stepOffline_->Clear();

    stepOffline_->Add(Lbl(L"离线登录", 14.f, T2(*ctx_)));

    auto input = std::make_shared<vcui::Input>(L"输入玩家名，例如 Steve");
    input->W = -1.f;
    input->Grow = 1.f;
    input->Submitted = [self, input] {
        std::wstring raw = Trim(input->Text);
        if (!ValidPlayerName(raw)) {
            self->SetPanelStatus(L"名字需为 3–16 位，只能用字母、数字、下划线。", true);
            return;
        }
        AccountEntry e;
        e.type = L"offline";
        e.name = raw;
        e.uuid = OfflineUuidOf(raw);
        AddAccount(*self->ctx_->settings, e);
        self->CloseLoginPanel();
        self->NotifyChanged(L"已登录离线账户 " + raw);
    };
    stepOffline_->Add(input);

    statusOff_ = Lbl(L"", 11.f, T3(*ctx_), -1.f, false);
    statusOff_->Wrap = true;
    statusOff_->H = 28.f;
    stepOffline_->Add(statusOff_);

    auto btns = vcui::HBox(8.f);
    btns->W = -1.f;
    auto back = std::make_shared<vcui::Button>(L"返回");
    back->W = 84.f;
    back->Clicked = [self] { self->ShowStep(0); };
    auto sp = std::make_shared<vcui::Widget>();
    sp->Grow = 1.f;
    auto ok = std::make_shared<vcui::Button>(L"登录", true);
    ok->W = 96.f;
    ok->Clicked = [input] { if (input->Submitted) input->Submitted(); };
    btns->Add(back);
    btns->Add(sp);
    btns->Add(ok);
    stepOffline_->Add(btns);
}

void AccountPage::BuildSecretStep() {
    auto self = this;
    stepSecret_->Clear();

    auto seg = std::make_shared<vcui::Segmented>(std::vector<std::wstring>{ L"密码登录", L"OAuth2 登录" }, 0);
    seg->W = 240.f;
    seg->Changed = [self](int i) { self->ShowStep(i == 0 ? 2 : 3); };
    stepSecret_->Add(seg);

    auto user = std::make_shared<vcui::Input>(L"用户名或邮箱");
    user->W = -1.f;
    user->Grow = 1.f;
    stepSecret_->Add(user);

    auto pass = std::make_shared<vcui::Input>(L"密码");
    pass->W = -1.f;
    pass->Grow = 1.f;
    stepSecret_->Add(pass);

    statusSecret_ = Lbl(L"", 11.f, T3(*ctx_), -1.f, false);
    statusSecret_->Wrap = true;
    statusSecret_->H = 28.f;
    stepSecret_->Add(statusSecret_);

    auto btns = vcui::HBox(8.f);
    btns->W = -1.f;
    auto back = std::make_shared<vcui::Button>(L"返回");
    back->W = 84.f;
    back->Clicked = [self] { self->ShowStep(0); };
    auto sp = std::make_shared<vcui::Widget>();
    sp->Grow = 1.f;
    auto ok = std::make_shared<vcui::Button>(L"登录", true);
    ok->W = 96.f;
    btns->Add(back);
    btns->Add(sp);
    btns->Add(ok);
    stepSecret_->Add(btns);

    auto submit = [self, user, pass, ok] {
        if (!ok->Enabled) return;
        std::wstring u = Trim(user->Text);
        std::wstring p = pass->Text;
        if (u.empty() || p.empty()) {
            self->SetPanelStatus(L"账号和密码都要填。", true);
            return;
        }
        ok->Enabled = false;
        self->SetPanelStatus(L"正在向 LittleSkin 登录…", false);
        self->StartLittleSkinPassword(u, p, ok);
    };
    ok->Clicked = [submit] { submit(); };
    pass->Submitted = [submit] { submit(); };
}

void AccountPage::BuildOAuthStep() {
    auto self = this;
    stepOAuth_->Clear();

    auto seg = std::make_shared<vcui::Segmented>(std::vector<std::wstring>{ L"密码登录", L"OAuth2 登录" }, 1);
    seg->W = 240.f;
    seg->Changed = [self](int i) { self->ShowStep(i == 0 ? 2 : 3); };
    stepOAuth_->Add(seg);

    statusOAuth_ = Lbl(L"", 11.f, T3(*ctx_), -1.f, false);
    statusOAuth_->Wrap = true;
    statusOAuth_->H = 28.f;
    stepOAuth_->Add(statusOAuth_);

    auto btns = vcui::HBox(8.f);
    btns->W = -1.f;
    auto back = std::make_shared<vcui::Button>(L"返回");
    back->W = 84.f;
    back->Clicked = [self] {
        self->oauth_.reset();
        self->ShowStep(0);
    };
    auto sp = std::make_shared<vcui::Widget>();
    sp->Grow = 1.f;
    auto go = std::make_shared<vcui::Button>(L"打开浏览器授权", true);
    go->W = 150.f;
    btns->Add(back);
    btns->Add(sp);
    btns->Add(go);
    stepOAuth_->Add(btns);

    go->Clicked = [self] { self->StartOAuthAuthorize(L"1555"); };
}

void AccountPage::ShowStep(int step) {
    if (!panelBody_) return;
    if (step != 3 && oauth_) oauth_.reset();
    if (stepMethods_) stepMethods_->Visible = (step == 0);
    if (stepOffline_) stepOffline_->Visible = (step == 1);
    if (stepSecret_) stepSecret_->Visible = (step == 2);
    if (stepOAuth_) stepOAuth_->Visible = (step == 3);

    panelStatus_ = (step == 1) ? statusOff_ : (step == 2) ? statusSecret_ : (step == 3) ? statusOAuth_ : nullptr;
    if (panelStatus_) {
        panelStatus_->Text.clear();
        panelStatus_->Color = T3(*ctx_);
    }
    if (ctx_->surface) ctx_->surface->Invalidate();
}

void AccountPage::SetPanelStatus(const std::wstring& text, bool error) {
    if (!panelStatus_) return;
    panelStatus_->Text = text;
    panelStatus_->Color = error ? ErrColor() : T3(*ctx_);
    if (ctx_->surface) ctx_->surface->Invalidate();
}

void AccountPage::CloseLoginPanel() {
    if (ctx_ && ctx_->surface && panelRoot_) {
        ctx_->surface->ClearDismissIf(panelRoot_);
        ctx_->surface->RemoveOverlayDeferred(panelRoot_);
    }
    if (oauth_) oauth_.reset();
    panelStatus_.reset();
    statusOff_.reset();
    statusSecret_.reset();
    statusOAuth_.reset();
    panelRoot_ = nullptr;
    panelBody_.reset();
    stepMethods_.reset();
    stepOffline_.reset();
    stepSecret_.reset();
    stepOAuth_.reset();
    if (ctx_->surface) ctx_->surface->Invalidate();
}

void AccountPage::StartLittleSkinPassword(const std::wstring& user, const std::wstring& pass,
                                          std::shared_ptr<vcui::Button> okBtn) {
    auto self = this;
    std::string body = "{\"username\":\"" + JsonEscape(user) + "\",\"password\":\"" + JsonEscape(pass) +
                       "\",\"clientToken\":\"" + WideToUtf8(NewGuid()) + "\",\"requestUser\":true}";

    std::wstring url = std::wstring(ls::Yggdrasil()) + L"/authserver/authenticate";
    ctx_->http->Post(url, body,
                     [self, okBtn](bool ok, const std::string& resp, const std::string& err) {
                         if (!ok) {
                             self->SetPanelStatus(ls::ErrorOf(resp, L"网络错误：" + Utf8ToWide(err)), true);
                             if (okBtn) okBtn->Enabled = true;
                             return;
                         }
                         AccountEntry e;
                         std::wstring perr;
                         if (!ls::ParseSession(resp, e, perr)) {
                             self->SetPanelStatus(perr, true);
                             if (okBtn) okBtn->Enabled = true;
                             return;
                         }
                         e.server = ls::Yggdrasil();
                         AddAccount(*self->ctx_->settings, e);
                         self->CloseLoginPanel();
                         self->NotifyChanged(L"已登录 LittleSkin 账户 " + e.name);
                     },
                     { { L"Content-Type", L"application/json" } }, 25000);
}

// OAuth2 走设备代码流：LittleSkin 的授权代码流要求 client_secret（公共客户端拿不到，
// 也不支持 PKCE），而设备代码流只认 client_id，正是给启动器这类无后端应用用的。
void AccountPage::StartOAuthAuthorize(const std::wstring& clientId) {
    auto self = this;
    oauth_ = std::make_shared<OAuthFlow>();
    auto flow = oauth_;
    flow->clientId = clientId;
    flow->step = 0;

    SetPanelStatus(L"正在向 LittleSkin 申请授权码…", false);

    std::wstring scope = L"User.Read Yggdrasil.PlayerProfiles.Read Yggdrasil.MinecraftToken.Create";
    std::string body = "client_id=" + WideToUtf8(ls::UrlEncode(clientId)) +
                       "&scope=" + WideToUtf8(ls::UrlEncode(scope));
    std::wstring url = std::wstring(ls::OpenOrigin()) + L"/oauth/device_code";
    ctx_->http->Post(
        url, body,
        [self, flow](bool ok, const std::string& resp, const std::string& err) {
            if (self->oauth_ != flow) return;   // 弹窗已关 / 重开了新的流程
            if (!ok) {
                self->SetPanelStatus(L"申请授权码失败：" + ls::ErrorOf(resp, Utf8ToWide(err)), true);
                self->oauth_.reset();
                return;
            }
            flow->userCode = ls::JsonStr(resp, "user_code");
            flow->deviceCode = ls::JsonStr(resp, "device_code");
            flow->verifyUrl = ls::JsonStr(resp, "verification_uri_complete");
            if (flow->verifyUrl.empty()) flow->verifyUrl = ls::JsonStr(resp, "verification_uri");
            if (flow->deviceCode.empty() || flow->userCode.empty()) {
                self->SetPanelStatus(ls::ErrorOf(resp, L"没有拿到设备代码"), true);
                self->oauth_.reset();
                return;
            }

            unsigned long long now = GetTickCount64();
            unsigned interval = (unsigned)ls::JsonInt(resp, "interval", 5);
            unsigned expires = (unsigned)ls::JsonInt(resp, "expires_in", 300);
            flow->pollMs = (interval ? interval : 5) * 1000;
            flow->nextPollMs = now + flow->pollMs;
            flow->expiresMs = now + (unsigned long long)expires * 1000;
            flow->step = 1;

            self->SetPanelStatus(L"授权码 " + flow->userCode + L"，浏览器确认后自动继续。", false);

            std::wstring open = flow->verifyUrl;
            if (!open.empty()) {
                std::thread([open] {
                    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                    ShellExecuteW(nullptr, L"open", open.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    CoUninitialize();
                }).detach();
            }
            if (self->ctx_->surface) self->ctx_->surface->Invalidate();
        },
        { { L"Content-Type", L"application/x-www-form-urlencoded" } }, 20000);
}

bool AccountPage::Tick() {
    if (!oauth_) return false;
    auto flow = oauth_;
    if (flow->step == 1) {
        unsigned long long now = GetTickCount64();
        if (now > flow->expiresMs) {
            SetPanelStatus(L"授权码已过期，请重新发起授权。", true);
            oauth_.reset();
            return false;
        }
        if (now >= flow->nextPollMs) {
            flow->nextPollMs = now + flow->pollMs;   // 先排下一次，免得请求慢时每帧重发
            PollDeviceToken();
        }
    }
    return true;    // 等待/请求期间保持帧循环
}

void AccountPage::PollDeviceToken() {
    auto self = this;
    auto flow = oauth_;
    if (!flow) return;

    std::string body = "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code&client_id=" +
                       WideToUtf8(ls::UrlEncode(flow->clientId)) +
                       "&device_code=" + WideToUtf8(ls::UrlEncode(flow->deviceCode));
    std::wstring url = std::wstring(ls::OpenOrigin()) + L"/oauth/token";
    ctx_->http->Post(
        url, body,
        [self, flow](bool ok, const std::string& resp, const std::string& err) {
            if (self->oauth_ != flow) return;
            if (!ok) {
                // 网络抖动不算授权失败，等下一轮再试
                return;
            }
            std::wstring e = ls::JsonStr(resp, "error");
            if (e == L"authorization_pending") return;
            if (e == L"slow_down") {
                flow->pollMs += 5000;
                return;
            }
            if (!e.empty()) {
                self->SetPanelStatus(ls::ErrorOf(resp, L"授权失败"), true);
                self->oauth_.reset();
                return;
            }
            std::wstring token = ls::JsonStr(resp, "access_token");
            if (token.empty()) {
                self->SetPanelStatus(ls::ErrorOf(resp, L"没有拿到 access_token"), true);
                self->oauth_.reset();
                return;
            }
            flow->step = 2;
            self->SetPanelStatus(L"授权完成，正在读取 LittleSkin 角色…", false);
            self->FetchOAuthProfiles(token);
        },
        { { L"Content-Type", L"application/x-www-form-urlencoded" } }, 20000);
}

void AccountPage::FetchOAuthProfiles(const std::wstring& bearer) {
    auto self = this;
    std::wstring url = std::wstring(ls::Yggdrasil()) + L"/sessionserver/session/minecraft/profile";
    ctx_->http->Get(url,
                    [self, bearer](bool ok, const std::string& resp, const std::string& err) {
                        if (!ok) {
                            self->SetPanelStatus(L"读取角色失败：" + Utf8ToWide(err), true);
                            self->oauth_.reset();
                            return;
                        }
                        std::wstring id, name, perr;
                        if (!ls::ParseProfiles(resp, id, name, perr)) {
                            self->SetPanelStatus(perr, true);
                            self->oauth_.reset();
                            return;
                        }
                        self->SetPanelStatus(L"正在换取 Minecraft 令牌…", false);
                        self->FetchMinecraftToken(bearer, id, name);
                    },
                    { { L"Authorization", L"Bearer " + bearer } }, 25000);
}

void AccountPage::FetchMinecraftToken(const std::wstring& bearer, const std::wstring& uuid,
                                      const std::wstring& name) {
    auto self = this;
    std::string body = "{\"uuid\":\"" + WideToUtf8(uuid) + "\"}";
    std::wstring url = std::wstring(ls::Yggdrasil()) + L"/authserver/oauth";
    ctx_->http->Post(
        url, body,
        [self, name](bool ok, const std::string& resp, const std::string& err) {
            if (!ok) {
                self->SetPanelStatus(L"换取 Minecraft 令牌失败：" + ls::ErrorOf(resp, Utf8ToWide(err)), true);
                self->oauth_.reset();
                return;
            }
            AccountEntry e;
            std::wstring perr;
            if (!ls::ParseSession(resp, e, perr)) {
                self->SetPanelStatus(perr, true);
                self->oauth_.reset();
                return;
            }
            if (e.name.empty()) e.name = name;
            e.server = ls::Yggdrasil();
            self->CompleteOAuth(e);
        },
        { { L"Authorization", L"Bearer " + bearer }, { L"Content-Type", L"application/json" } }, 25000);
}

void AccountPage::CompleteOAuth(const AccountEntry& e) {
    AddAccount(*ctx_->settings, e);
    CloseLoginPanel();
    NotifyChanged(L"已登录 LittleSkin 账户 " + e.name);
}

} // namespace vcl
