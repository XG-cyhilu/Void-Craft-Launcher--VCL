#include "ui/Common.h"

#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace vcl {

void UiCtx::Toast(const std::wstring& text, vcui::InfoTone tone) {
    // 默认落到状态栏；只有没接状态栏时（比如跑自检）才直接弹 InfoBar
    if (operation) {
        operation(text, tone);
        return;
    }
    if (surface) surface->ShowInfoBar(text, tone, 3.5f);
}

std::shared_ptr<vcui::Label> Text(const std::wstring& s, const vcui::TypeStyle& st) {
    auto l = std::make_shared<vcui::Label>(s);
    l->UseType(st);
    return l;
}

std::shared_ptr<vcui::Label> Caption(const std::wstring& s) {
    auto l = std::make_shared<vcui::Label>(s);
    l->UseType(vcui::Type::Caption1);
    l->Ellipsis = true;
    return l;
}

std::shared_ptr<vcui::Stack> Row(float gap) { return vcui::HBox(gap); }
std::shared_ptr<vcui::Stack> Col(float gap) { return vcui::VBox(gap); }

std::shared_ptr<vcui::Widget> VSpace(float h) { return vcui::Spacer(h); }

std::shared_ptr<vcui::Widget> PageHeader(const std::wstring& title, const std::wstring& sub) {
    auto box = Col(4.f);
    box->Add(Text(title, vcui::Type::Title3));
    if (!sub.empty()) box->Add(Caption(sub));
    return box;
}

bool PickFolder(std::wstring& out) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return false;

    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"选择文件夹");

    if (FAILED(dlg->Show(nullptr))) return false;

    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return false;
    wchar_t* path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return false;
    out = path;
    CoTaskMemFree(path);
    return true;
}

std::shared_ptr<vcui::Card> OpenPanel(vcui::Surface* host, const std::wstring& title,
                                      float width, float height, std::function<void()> onClose,
                                      std::shared_ptr<vcui::Stack>* contentOut) {
    auto card = std::make_shared<vcui::Card>();
    card->W = width;
    card->H = height;

    auto body = Col(10.f);
    body->Pad(20.f);

    auto head = Row(8.f);
    head->Add(Text(title, vcui::Type::Subtitle2));
    auto sp = vcui::Spacer(0.f);
    sp->Grow = 1.f;
    head->Add(sp);
    auto close = std::make_shared<vcui::IconButton>(L"\xE711", true);
    close->Tooltip = L"关闭";
    head->Add(close);
    body->Add(head);
    body->Add(std::make_shared<vcui::Divider>());

    auto content = Col(8.f);
    content->Grow = 1.f;
    body->Add(content);
    card->Add(body);
    if (contentOut) *contentOut = content;

    // 覆盖层根：铺满客户区，纵向居中放卡片（Card 自己不是 FillHost，尺寸才有效）
    auto root = vcui::VBox(0.f);
    root->FillHost = true;
    root->Cross = vcui::Stack::Center;
    root->Pad(24.f);
    auto top = vcui::Spacer(0.f);
    top->Grow = 1.f;
    auto bottom = vcui::Spacer(0.f);
    bottom->Grow = 1.f;
    root->Add(top);
    root->Add(card);
    root->Add(bottom);

    host->AddOverlay(root);
    host->SetDismiss([host, root, onClose] {
        host->RemoveOverlayDeferred(root.get());
        if (onClose) onClose();
    }, root.get());

    auto hostPtr = host;
    close->Clicked = [hostPtr, root, onClose] {
        hostPtr->ClearDismissIf(root.get());
        hostPtr->RemoveOverlayDeferred(root.get());
        if (onClose) onClose();
    };
    return card;
}

} // namespace vcl
