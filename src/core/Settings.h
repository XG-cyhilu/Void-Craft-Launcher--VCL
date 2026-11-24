#pragma once
#include <string>
#include <vector>

// global.json 的 C++ 侧视图（见 docs/ARCHITECTURE.md §3.1）。
// 只放 v0.1 真正会读写的字段，其余保持配置里的原样（Save 时按已知键重写）。
struct Settings {
    std::vector<std::pair<std::wstring, std::wstring>> roots;   // path, name
    int currentRoot = 0;

    std::wstring language = L"zh-CN";
    std::wstring theme = L"auto";        // auto / light / dark
    double opacity = 0.9;
    std::wstring accent = L"#0071EA";

    std::wstring mirror = L"bmclapi";    // bmclapi / mojang
    int concurrency = 16;
    bool animations = true;

    std::wstring javaPath;
    std::wstring cacheDir;               // 空 = 用默认缓存目录
};

bool LoadSettings(Settings& out, std::string* err = nullptr);
bool SaveSettings(const Settings& s, std::string* err = nullptr);

// 当前 Root 的绝对路径；roots 为空时返回默认 .minecraft
std::wstring CurrentRootPath(const Settings& s);

// 界面设置里改主题/强调色后统一调用这个，把值落到 Surface
namespace vcui { class Surface; }
void ApplyThemeToSurface(const Settings& s, vcui::Surface* host);
