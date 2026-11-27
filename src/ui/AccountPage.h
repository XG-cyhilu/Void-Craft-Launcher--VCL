#pragma once
#include <memory>
#include <string>

#include "core/OAuthLoop.h"
#include "ui/Common.h"

namespace vcl {

// 管理 · 账户：多账户列表 + 弹窗式登录（离线 / LittleSkin / Microsoft）。
// LittleSkin 提供密码登录（账号 + 密码，走 Yggdrasil authenticate）与 OAuth2 浏览器授权；
// Microsoft 先占位。
class AccountPage {
public:
    explicit AccountPage(UiCtx* ctx);

    static std::vector<std::wstring> SubNav();
    std::shared_ptr<vcui::Widget> Build(int sub);

    // 帧泵钩子：OAuth 回调是后台线程收的，这里每帧取一次结果（返回 true 表示还在等）
    bool Tick();

    // 打开登录弹窗（自检钩子也用它）
    void OpenLogin();

private:
    struct OAuthFlow {
        std::shared_ptr<OAuthLoop> loop;
        std::wstring clientId;
        std::wstring clientSecret;
        std::wstring redirect;
        std::wstring code;
        int step = 0;    // 0 等回调 / 1 换 token / 2 读角色 / 3 换 MC 令牌
    };

    std::shared_ptr<vcui::Widget> BuildPage();
    void FillList();
    void NotifyChanged(const std::wstring& toast);

    // 登录弹窗：四个步骤面板一次建好，切换只改 Visible，
    // 这样点击回调里不会清掉自己所在的控件树（否则弹窗会被遮罩层当成"点了外面"关掉）。
    void BuildSteps();
    void BuildMethodsStep();
    void BuildOfflineStep();
    void BuildSecretStep();
    void BuildOAuthStep();
    void ShowStep(int step);
    void CloseLoginPanel();
    void SetPanelStatus(const std::wstring& text, bool error);

    void StartLittleSkinPassword(const std::wstring& user, const std::wstring& pass,
                                 std::shared_ptr<vcui::Button> okBtn);
    void StartOAuthAuthorize(const std::wstring& clientId, const std::wstring& clientSecret);
    void ExchangeOAuthCode();
    void FetchOAuthProfiles(const std::wstring& bearer);
    void FetchMinecraftToken(const std::wstring& bearer, const std::wstring& uuid,
                             const std::wstring& name);
    void CompleteOAuth(const AccountEntry& e);

    UiCtx* ctx_ = nullptr;

    std::shared_ptr<vcui::Stack> list_;
    std::shared_ptr<vcui::Label> hint_;
    std::shared_ptr<vcui::Badge> count_;

    std::shared_ptr<vcui::Stack> panelBody_;     // 登录弹窗内容列
    vcui::Widget* panelRoot_ = nullptr;          // 覆盖层根（只借指针，生命周期归 Surface）
    std::shared_ptr<vcui::Stack> stepMethods_;
    std::shared_ptr<vcui::Stack> stepOffline_;
    std::shared_ptr<vcui::Stack> stepSecret_;
    std::shared_ptr<vcui::Stack> stepOAuth_;
    std::shared_ptr<vcui::Label> panelStatus_;   // 当前步骤的状态行
    std::shared_ptr<vcui::Label> statusOff_;
    std::shared_ptr<vcui::Label> statusSecret_;
    std::shared_ptr<vcui::Label> statusOAuth_;
    std::shared_ptr<OAuthFlow> oauth_;
};

} // namespace vcl
