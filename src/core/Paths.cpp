#include "core/Paths.h"

#include <windows.h>
#include <shlobj.h>
#include <filesystem>

namespace fs = std::filesystem;

static std::wstring EnvDir(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) {
        (void)name;
        return std::wstring(buf);
    }
    return std::wstring();
}

std::wstring AppConfigDir() {
    std::wstring appData;
    wchar_t buf[MAX_PATH * 2];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf)))
        appData = buf;
    else {
        const wchar_t* e = _wgetenv(L"APPDATA");
        appData = e ? e : L"";
    }
    return JoinPath(JoinPath(appData, L"XGStudio"), L"VoidCraftLauncher");
}

std::wstring AppCacheDir() {
    std::wstring local;
    wchar_t buf[MAX_PATH * 2];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf)))
        local = buf;
    else {
        const wchar_t* e = _wgetenv(L"LOCALAPPDATA");
        local = e ? e : L"";
    }
    return JoinPath(JoinPath(local, L"XGStudio"), L"VoidCraftLauncher");
}

std::wstring GlobalConfigPath() { return JoinPath(AppConfigDir(), L"global.json"); }

std::wstring HttpCacheDir() { return JoinPath(AppCacheDir(), L"http"); }
std::wstring IconCacheDir() { return JoinPath(AppCacheDir(), L"icons"); }

std::wstring ResourceDir() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    wchar_t exe[MAX_PATH * 2];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH * 2) == 0) return L"";
    std::wstring dir = exe;
    size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash);
    std::wstring local = JoinPath(dir, L"resource");
    if (DirExists(local)) { cached = local; return cached; }
    // 开发期直接在 .build\Release 里跑：往源码树的 resource 上回退
    std::wstring up = dir + L"\\..\\..\\resource";
    if (DirExists(up)) { cached = NormalizePath(up); return cached; }
    cached = local;
    return cached;
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::wstring out = a;
    if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    out += b;
    return out;
}

void EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::path(dir), ec);
}

bool DirExists(const std::wstring& dir) {
    std::error_code ec;
    return !dir.empty() && fs::is_directory(fs::path(dir), ec);
}

bool FileExists(const std::wstring& file) {
    std::error_code ec;
    return !file.empty() && fs::is_regular_file(fs::path(file), ec);
}

void ClearTree(const std::wstring& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    if (!DirExists(dir)) return;
    for (auto it = fs::directory_iterator(fs::path(dir), ec); !ec && it != fs::directory_iterator(); ) {
        std::error_code ec2;
        fs::remove_all(it->path(), ec2);          // 删失败（文件被占用）就跳过这一项，不清空整棵树
        it.increment(ec);
    }
}

std::wstring DefaultRootPath() {
    std::wstring appData;
    wchar_t buf[MAX_PATH * 2];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf)))
        appData = buf;
    else {
        const wchar_t* e = _wgetenv(L"APPDATA");
        appData = e ? e : L"";
    }
    return JoinPath(appData, L".minecraft");
}

std::wstring NormalizePath(const std::wstring& in) {
    std::wstring s = in;
    // 去掉首尾引号与空白
    while (!s.empty() && (s.front() == L'"' || s.front() == L' ')) s.erase(s.begin());
    while (!s.empty() && (s.back() == L'"' || s.back() == L' ')) s.pop_back();
    if (s.empty()) return s;
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(s), ec);
    if (ec) return s;
    std::wstring out = p.lexically_normal().wstring();
    while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) out.pop_back();
    return out;
}


