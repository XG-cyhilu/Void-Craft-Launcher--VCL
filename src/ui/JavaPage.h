#pragma once
#include <memory>

#include "ui/Common.h"

namespace vcl {

// 管理 · Java：PATH / 手动添加的 Java 一览（JDK/JRE、x64/x86、发行商），
// 点一行就设为启动用 Java；下载走弹窗，按厂商动态拉 LTS 版本。
class JavaPage {
public:
    explicit JavaPage(UiCtx* ctx);

    static std::vector<std::wstring> SubNav();
    std::shared_ptr<vcui::Widget> Build(int sub);

    struct State;
    std::shared_ptr<State> state_;   // 跨页重建保留：下载/解压状态不能丢
    UiCtx* ctx_ = nullptr;

private:
    void ScanPath(bool notify);      // 扫 PATH / JAVA_HOME / 自己的 java 目录 / 手动添加
    void RefreshList();              // 重建列表
    void OpenDownload();             // 打开下载弹窗
    void LoadMajors();               // 异步拉厂商 LTS 版本
    void FetchRelease();             // 异步拉某个版本的可下载包
    void StartInstall();
    bool TickDownload();             // 每帧推进下载/解压
};

} // namespace vcl
