#pragma once
#include <string>
#include <vector>

#include <d2d1.h>
#include "ui/styles/token.h"

// 一个账户条目。离线账户的 uuid 由玩家名按 Java 离线规则生成；
// LittleSkin 账户存 Yggdrasil 令牌，uuid 是角色 id（32 位无横线）。
struct AccountEntry {
    std::wstring type = L"offline";      // offline / littleskin
    std::wstring name;                   // 玩家名
    std::wstring uuid;
    std::wstring accessToken;            // LittleSkin 有效令牌
    std::wstring clientToken;
    std::wstring server = L"https://littleskin.cn/api/yggdrasil";
};

// global.json 的 C++ 侧视图（见 docs/ARCHITECTURE.md §3.1）。
// 只放 v0.1 真正会读写的字段，其余保持配置里的原样（Save 时按已知键重写）。
struct Settings {
    std::vector<std::pair<std::wstring, std::wstring>> roots;   // path, name
    int currentRoot = 0;

    std::wstring language = L"zh-CN";
    std::wstring theme = L"auto";        // auto / light / dark
    double opacity = 0.9;
    std::wstring accent = vcl::tok::AccentHex;
    std::wstring accountName = L"Player";   // 当前生效账户名（状态栏读它；改账户时同步）

    std::vector<AccountEntry> accounts;     // 账户列表（多账户）
    int currentAccount = 0;                 // 当前生效账户下标

    std::wstring mirror = L"bmclapi";    // bmclapi / mojang
    int concurrency = 16;
    int fileThreads = 8;                 // 单文件下载线程（1~128），大文件才分块并行
    int chunkThresholdKb = 1024;         // 超过该大小才走分块（128 KB ~ 8 MB）
    bool statusAccent = false;           // 状态栏用强调色铺底（文字自动反色）
    bool animations = true;
    bool listZebra = false;              // 列表隔行浅底（默认关，纯靠悬停区分）

    std::wstring javaPath;               // 选来启动游戏的那个 java（javaw.exe 绝对路径）
    std::vector<std::wstring> javaExtra; // 手动添加过的 Java 路径（下次启动仍然认识）
    std::wstring javaDir;                // 自定义的 Java 安装/下载目录（空 = 启动器 java 目录）
    // 深度扫描不进的目录（前缀匹配）；内置默认还会加 Windows / Users 等
    std::vector<std::wstring> javaDeepExcludes;
    std::wstring cacheDir;               // 空 = 用默认缓存目录
};

bool LoadSettings(Settings& out, std::string* err = nullptr);
bool SaveSettings(const Settings& s, std::string* err = nullptr);

// 当前 Root 的绝对路径；roots 为空时返回默认 .minecraft
std::wstring CurrentRootPath(const Settings& s);

// 强调色（#RRGGBB）→ D2D1 颜色
D2D1_COLOR_F AccentColor(const Settings& s);

// 界面设置里改主题/强调色后统一调用这个，把值落到 Surface
namespace vcui { class Surface; }
void ApplyThemeToSurface(const Settings& s, vcui::Surface* host);
