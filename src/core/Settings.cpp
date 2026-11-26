#include "core/Settings.h"
#include "core/Paths.h"
#include "core/Text.h"

#include <windows.h>
#include "xgcfg/xgcfg.h"
#include "vcui/Surface.h"

static int HexColor(const std::wstring& s, unsigned def) {
    std::wstring t = s;
    if (!t.empty() && t[0] == L'#') t.erase(t.begin());
    if (t.size() != 6) return (int)def;
    unsigned v = 0;
    if (swscanf_s(t.c_str(), L"%6X", &v) != 1) return (int)def;
    return (int)v;
}

static std::wstring HexFromLong(long long v) {
    return WFormat(L"#%06llX", (unsigned long long)(v & 0xFFFFFF));
}

bool LoadSettings(Settings& out, std::string* err) {
    xgcfg::Config cfg;
    std::string e;
    if (!cfg.Load(GlobalConfigPath(), &e)) {
        if (err) *err = e;
        return false;
    }
    const xgcfg::Value& r = cfg.Root();

    out.roots.clear();
    if (const xgcfg::Value* arr = r.Find("roots"); arr && arr->IsArray()) {
        for (size_t i = 0; i < arr->Size(); ++i) {
            const xgcfg::Value& it = arr->At(i);
            const xgcfg::Value* p = it.Find("path");
            if (!p) continue;
            const xgcfg::Value* n = it.Find("name");
            std::wstring path = Utf8ToWide(p->AsString());
            std::wstring name = n ? Utf8ToWide(n->AsString()) : path;
            if (!path.empty()) out.roots.push_back({ path, name });
        }
    }
    out.currentRoot = 0;
    if (const xgcfg::Value* v = r.FindPath("currentRoot"); v && v->IsNumber())
        out.currentRoot = (int)v->AsInt(0);
    if (out.currentRoot < 0 || out.currentRoot >= (int)out.roots.size()) out.currentRoot = 0;

    auto strOf = [&r](const char* path, const std::wstring& def) {
        const xgcfg::Value* v = r.FindPath(path);
        if (!v || !v->IsString()) return def;
        return Utf8ToWide(v->AsString());
    };
    out.language = strOf("ui.language", L"zh-CN");
    out.theme = strOf("ui.theme", L"auto");
    out.accent = strOf("ui.accent", L"#0071EA");
    out.mirror = strOf("download.mirror", L"bmclapi");
    out.javaPath = strOf("java.preferredPath", L"");
    out.javaDir = strOf("java.installDir", L"");
    out.javaExtra.clear();
    if (const xgcfg::Value* arr = r.Find("java.extraPaths"); arr && arr->IsArray()) {
        for (size_t i = 0; i < arr->Size(); ++i) {
            std::wstring p = Utf8ToWide(arr->At(i).AsString());
            if (!p.empty()) out.javaExtra.push_back(p);
        }
    }
    out.cacheDir = strOf("cache.dir", L"");

    if (const xgcfg::Value* v = r.FindPath("ui.opacity"); v && v->IsNumber()) out.opacity = v->AsDouble(0.9);
    if (const xgcfg::Value* v = r.FindPath("ui.animations"); v && v->IsBool()) out.animations = v->AsBool(true);
    if (const xgcfg::Value* v = r.FindPath("ui.statusAccent"); v && v->IsBool()) out.statusAccent = v->AsBool(false);
    if (const xgcfg::Value* v = r.FindPath("ui.listZebra"); v && v->IsBool()) out.listZebra = v->AsBool(false);
    if (const xgcfg::Value* v = r.FindPath("download.concurrency"); v && v->IsNumber())
        out.concurrency = (int)v->AsInt(16);
    if (const xgcfg::Value* v = r.FindPath("download.fileThreads"); v && v->IsNumber())
        out.fileThreads = (int)v->AsInt(8);
    if (out.fileThreads < 1) out.fileThreads = 1;
    if (out.fileThreads > 128) out.fileThreads = 128;
    if (const xgcfg::Value* v = r.FindPath("download.chunkThresholdKb"); v && v->IsNumber())
        out.chunkThresholdKb = (int)v->AsInt(1024);
    if (out.chunkThresholdKb < 128) out.chunkThresholdKb = 128;
    if (out.chunkThresholdKb > 8192) out.chunkThresholdKb = 8192;
    return true;
}

bool SaveSettings(const Settings& s, std::string* err) {
    xgcfg::Config cfg;
    cfg.Root().EnsureObject();
    cfg.Set("version", xgcfg::Value(1));

    xgcfg::Value roots;
    roots.EnsureArray();
    for (const auto& r : s.roots) {
        xgcfg::Value item;
        item.EnsureObject();
        item.SetKey("path", xgcfg::Value(WideToUtf8(r.first)));
        item.SetKey("name", xgcfg::Value(WideToUtf8(r.second)));
        roots.Push(std::move(item));
    }
    cfg.Root().SetKey("roots", std::move(roots));
    cfg.Set("currentRoot", xgcfg::Value((int64_t)s.currentRoot));

    cfg.Set("ui.language", xgcfg::Value(WideToUtf8(s.language)));
    cfg.Set("ui.theme", xgcfg::Value(WideToUtf8(s.theme)));
    cfg.Set("ui.opacity", xgcfg::Value(s.opacity));
    cfg.Set("ui.accent", xgcfg::Value(WideToUtf8(s.accent)));
    cfg.Set("ui.animations", xgcfg::Value(s.animations));
    cfg.Set("ui.statusAccent", xgcfg::Value(s.statusAccent));
    cfg.Set("ui.listZebra", xgcfg::Value(s.listZebra));

    cfg.Set("download.mirror", xgcfg::Value(WideToUtf8(s.mirror)));
    cfg.Set("download.concurrency", xgcfg::Value((int64_t)s.concurrency));
    cfg.Set("download.fileThreads", xgcfg::Value((int64_t)s.fileThreads));
    cfg.Set("download.chunkThresholdKb", xgcfg::Value((int64_t)s.chunkThresholdKb));

    cfg.Set("java.autoDetect", xgcfg::Value(true));
    cfg.Set("java.preferredPath", xgcfg::Value(WideToUtf8(s.javaPath)));
    cfg.Set("java.installDir", xgcfg::Value(WideToUtf8(s.javaDir)));
    xgcfg::Value extra;
    extra.EnsureArray();
    for (const auto& p : s.javaExtra) extra.Push(xgcfg::Value(WideToUtf8(p)));
    cfg.Set("java.extraPaths", std::move(extra));
    if (!s.cacheDir.empty()) cfg.Set("cache.dir", xgcfg::Value(WideToUtf8(s.cacheDir)));

    EnsureDir(AppConfigDir());
    std::string e;
    bool ok = cfg.SaveAs(GlobalConfigPath(), true, &e);
    if (!ok && err) *err = e;
    return ok;
}

std::wstring CurrentRootPath(const Settings& s) {
    if (!s.roots.empty() && s.currentRoot >= 0 && s.currentRoot < (int)s.roots.size())
        return s.roots[(size_t)s.currentRoot].first;
    return DefaultRootPath();
}

D2D1_COLOR_F AccentColor(const Settings& s) {
    return vcui::Hex(HexColor(s.accent, 0x0071EA));
}

void ApplyThemeToSurface(const Settings& s, vcui::Surface* host) {
    if (!host) return;
    bool dark = false;
    if (s.theme == L"dark") dark = true;
    else if (s.theme == L"auto") {
        DWORD v = 0;
        // 读系统"应用模式"：0 浅色 / 1 深色
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                          0, KEY_READ, &k) == ERROR_SUCCESS) {
            DWORD val = 0, sz = sizeof(val), type = 0;
            if (RegQueryValueExW(k, L"AppsUseLightTheme", nullptr, &type, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
                v = val;
            RegCloseKey(k);
        }
        dark = (v == 0);
    }
    host->SetTheme(host->Th().pal, dark);
    D2D1_COLOR_F acc = AccentColor(s);
    host->SetAccent(acc);
}
