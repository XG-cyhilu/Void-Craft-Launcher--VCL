#include "ui/AccountPage.h"

#include <windows.h>
#include <wincrypt.h>

#include <cwctype>

#include "core/Text.h"

namespace vcl {

namespace {

D2D1_COLOR_F T2(const UiCtx& c) { return c.surface ? c.surface->Th().t.text2 : vcui::Hex(0x606060); }
D2D1_COLOR_F T3(const UiCtx& c) { return c.surface ? c.surface->Th().t.text3 : vcui::Hex(0x909090); }
D2D1_COLOR_F Acc(const UiCtx& c) { return c.surface ? c.surface->Th().t.accent : vcui::Hex(0x0071EA); }

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

// Java 离线账户的 UUID：nameUUIDFromBytes("OfflinePlayer:" + 名字) 的 MD5，
// 然后按 RFC 4122 打上 v3 与 variant 位。服务端认的就是这一串。
std::wstring OfflineUuid(const std::wstring& name) {
    const std::string seed = "OfflinePlayer:" + WideToUtf8(name);
    unsigned char md5[16]{};
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
            CryptHashData(hash, (const BYTE*)seed.data(), (DWORD)seed.size(), 0);
            DWORD len = 16;
            CryptGetHashParam(hash, HP_HASHVAL, md5, &len, 0);
            CryptDestroyHash(hash);
        }
        CryptReleaseContext(prov, 0);
    }
    md5[6] = (unsigned char)((md5[6] & 0x0F) | 0x30);   // version 3
    md5[8] = (unsigned char)((md5[8] & 0x3F) | 0x80);   // RFC variant

    auto hex = [](unsigned char b, int n) {
        static const wchar_t* d = L"0123456789abcdef";
        std::wstring s;
        for (int i = n - 1; i >= 0; --i) s.push_back(d[(b >> (i * 4)) & 0xF]);
        return s;
    };
    return hex(md5[0], 2) + hex(md5[1], 2) + hex(md5[2], 2) + hex(md5[3], 2) + L"-" +
           hex(md5[4], 2) + hex(md5[5], 2) + L"-" + hex(md5[6], 2) + hex(md5[7], 2) + L"-" +
           hex(md5[8], 2) + hex(md5[9], 2) + L"-" + hex(md5[10], 2) + hex(md5[11], 2) +
           hex(md5[12], 2) + hex(md5[13], 2) + hex(md5[14], 2) + hex(md5[15], 2);
}

bool ValidName(const std::wstring& s) {
    if (s.size() < 3 || s.size() > 16) return false;
    for (wchar_t c : s) {
        if (!(iswalnum(c) || c == L'_')) return false;
    }
    return true;
}

// 头像：强调色圆底 + 首字母。没有皮肤资源时不画假头，保持干净的字母头像。
class Avatar : public vcui::Widget {
public:
    std::wstring Name;
    float Box = 52.f;
    Avatar() {
        W = Box;
        H = Box;
    }
    D2D1_SIZE_F Desired(float, float) override { return D2D1::SizeF(Box, Box); }
    void Paint(vcui::PaintCtx& ctx) override {
        D2D1_RECT_F fr = Frame();
        float r = (fr.bottom - fr.top) * .5f;
        ctx.g.RoundRect(fr, r, vcui::A(ctx.t.accent, ctx.t.dark ? .30f : .14f));
        ctx.g.StrokeRect(fr, r, vcui::A(ctx.t.accent, .34f), 1.f);
        if (Name.empty()) return;
        wchar_t ch = (wchar_t)towupper(Name[0]);
        ctx.g.Text(fr, std::wstring(1, ch), 20.f, 600, ctx.t.accent, vcui::HAlign::Center,
                   vcui::VAlign::Middle);
    }
};

} // namespace

AccountPage::AccountPage(UiCtx* ctx) : ctx_(ctx) {}

std::vector<std::wstring> AccountPage::SubNav() { return { L"离线账户", L"正版账户" }; }

std::shared_ptr<vcui::Widget> AccountPage::Build(int sub) {
    (void)sub;
    auto page = Col(12.f);
    page->Grow = 1.f;

    auto& cfg = *ctx_->settings;
    const std::wstring name = cfg.accountName.empty() ? std::wstring(L"Player") : cfg.accountName;

    // ---------------- 离线账户 ----------------
    auto inner = vcui::VBox(0.f);

    auto head = vcui::HBox(8.f);
    head->PT = 12.f;
    head->PB = 10.f;
    head->PL = 16.f;
    head->PR = 16.f;
    head->W = -1.f;
    head->Cross = vcui::Stack::Center;
    head->Add(Lbl(L"离线账户", 13.f, T2(*ctx_)));
    auto sp = std::make_shared<vcui::Widget>();
    sp->Grow = 1.f;
    head->Add(sp);
    head->Add(std::make_shared<vcui::Badge>(L"离线", vcui::Badge::Warning));
    inner->Add(head);
    inner->Add(std::make_shared<vcui::Divider>());

    // 资料行：头像 + 名字 + UUID + 复制
    auto prof = vcui::HBox(16.f);
    prof->W = -1.f;
    prof->PT = 16.f;
    prof->PB = 16.f;
    prof->PL = 16.f;
    prof->PR = 16.f;
    prof->Cross = vcui::Stack::Center;

    auto avatar = std::make_shared<Avatar>();
    avatar->Name = name;
    prof->Add(avatar);

    auto info = vcui::VBox(4.f);
    info->Grow = 1.f;
    auto nameLbl = std::make_shared<vcui::Label>(name, 18.f, 600);
    info->Add(nameLbl);
    auto uuid = OfflineUuid(name);
    auto uuidLbl = Lbl(uuid, 12.f, T3(*ctx_), -1.f, true);
    info->Add(uuidLbl);
    info->Add(Lbl(L"离线 UUID（UUID v3，由玩家名生成）", 11.f, T3(*ctx_)));
    prof->Add(info);

    auto copy = std::make_shared<vcui::Button>(L"复制 UUID");
    copy->W = 108.f;
    prof->Add(copy);
    inner->Add(prof);
    inner->Add(std::make_shared<vcui::Divider>());

    // 改名
    auto edit = vcui::VBox(6.f);
    edit->W = -1.f;
    edit->PT = 14.f;
    edit->PB = 16.f;
    edit->PL = 16.f;
    edit->PR = 16.f;
    edit->Add(Lbl(L"玩家名称", 13.f, T2(*ctx_)));

    auto row = vcui::HBox(8.f);
    row->W = -1.f;
    row->Cross = vcui::Stack::Center;
    auto input = std::make_shared<vcui::Input>(L"3–16 位字母、数字或下划线");
    input->Text = name;
    input->W = -1.f;
    input->Grow = 1.f;
    auto apply = std::make_shared<vcui::Button>(L"保存", true);
    apply->W = 88.f;
    row->Add(input);
    row->Add(apply);
    edit->Add(row);

    auto hint = Lbl(L"改完名字 UUID 会跟着变；服务器与存档认的是 UUID，不是名字。", 11.f, T3(*ctx_));
    hint->Wrap = true;
    edit->Add(hint);
    inner->Add(edit);

    auto card = std::make_shared<vcui::Card>();
    card->Shadow = false;
    card->Border = true;
    card->Radius = 4.f;
    card->Pad(0.f);
    card->Add(inner);
    page->Add(card);

    auto self = this;

    copy->Clicked = [self, uuidLbl] {
        const std::wstring u = uuidLbl->Text;
        CopyText(u);
        self->ctx_->Toast(L"UUID 已复制到剪贴板", vcui::InfoTone::Success);
    };

    apply->Clicked = [self, input, avatar, nameLbl, uuidLbl, hint] {
        std::wstring raw = Trim(input->Text);
        if (!ValidName(raw)) {
            hint->Text = L"名称需为 3–16 位，只能用字母、数字、下划线。";
            hint->Color = self->ctx_->surface ? self->ctx_->surface->Th().t.accent : vcui::Hex(0xD13438);
            if (self->ctx_->surface) self->ctx_->surface->Invalidate();
            return;
        }
        auto& cfg2 = *self->ctx_->settings;
        cfg2.accountName = raw;
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        if (self->ctx_->applyUi) self->ctx_->applyUi();     // 状态栏的账户名跟着换

        avatar->Name = raw;
        nameLbl->Text = raw;
        uuidLbl->Text = OfflineUuid(raw);
        hint->Text = L"已保存。UUID 已按新名字重算。";
        hint->Color = T3(*self->ctx_);
        if (self->ctx_->surface) self->ctx_->surface->Invalidate();
        self->ctx_->Toast(L"离线账户已保存：" + raw, vcui::InfoTone::Success);
    };

    // 回车直接保存，别逼用户去点按钮
    input->Submitted = [apply] { if (apply->Clicked) apply->Clicked(); };

    // ---------------- 正版账户（占位） ----------------
    auto onlineInner = vcui::VBox(0.f);
    auto oh = vcui::HBox(8.f);
    oh->PT = 12.f;
    oh->PB = 10.f;
    oh->PL = 16.f;
    oh->PR = 16.f;
    oh->W = -1.f;
    oh->Cross = vcui::Stack::Center;
    oh->Add(Lbl(L"正版账户", 13.f, T2(*ctx_)));
    auto sp2 = std::make_shared<vcui::Widget>();
    sp2->Grow = 1.f;
    oh->Add(sp2);
    oh->Add(std::make_shared<vcui::Badge>(L"未接入", vcui::Badge::Neutral));
    onlineInner->Add(oh);
    onlineInner->Add(std::make_shared<vcui::Divider>());

    auto obody = vcui::VBox(10.f);
    obody->W = -1.f;
    obody->PT = 16.f;
    obody->PB = 16.f;
    obody->PL = 16.f;
    obody->PR = 16.f;
    auto otext = Lbl(L"Microsoft / Mojang 登录还在排期里。接入后会在这里显示正版档案、皮肤与披风，"
                     L"并支持多账户切换；现在启动游戏一律使用上面的离线身份。",
                     12.f, T3(*ctx_));
    otext->Wrap = true;
    obody->Add(otext);

    auto login = std::make_shared<vcui::Button>(L"使用 Microsoft 登录");
    login->W = 200.f;
    login->Enabled = false;
    login->Tooltip = L"未接入，先别点";
    obody->Add(login);
    onlineInner->Add(obody);

    auto ocard = std::make_shared<vcui::Card>();
    ocard->Shadow = false;
    ocard->Border = true;
    ocard->Radius = 4.f;
    ocard->Pad(0.f);
    ocard->Add(onlineInner);
    page->Add(ocard);

    return page;
}

} // namespace vcl
