#include "core/McApi.h"
#include "core/Text.h"

#include "xgcfg/xgcfg.h"

std::wstring McManifestUrl(bool bmclapi) {
    return bmclapi ? L"https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json"
                   : L"https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
}

bool ParseMcManifest(const std::string& json, std::vector<McVersion>& out, std::string* err) {
    xgcfg::Value root;
    std::string e;
    if (!xgcfg::Parse(json, root, &e)) {
        if (err) *err = e;
        return false;
    }
    const xgcfg::Value* arr = root.Find("versions");
    if (!arr || !arr->IsArray()) {
        if (err) *err = "manifest 缺少 versions";
        return false;
    }
    out.clear();
    out.reserve(arr->Size());
    for (size_t i = 0; i < arr->Size(); ++i) {
        const xgcfg::Value& v = arr->At(i);
        McVersion m;
        if (const xgcfg::Value* p = v.Find("id")) m.id = Utf8ToWide(p->AsString());
        if (const xgcfg::Value* p = v.Find("type")) m.type = Utf8ToWide(p->AsString());
        if (const xgcfg::Value* p = v.Find("releaseTime")) m.releaseTime = Utf8ToWide(p->AsString());
        if (const xgcfg::Value* p = v.Find("url")) m.url = Utf8ToWide(p->AsString());
        if (m.id.empty()) continue;
        out.push_back(std::move(m));
    }
    return true;
}

std::wstring MirrorUrl(const std::wstring& url, bool bmclapi) {
    if (!bmclapi || url.empty()) return url;
    struct Rule { const wchar_t* from; const wchar_t* to; };
    static const Rule rules[] = {
        { L"https://launchermeta.mojang.com", L"https://bmclapi2.bangbang93.com" },
        { L"https://piston-meta.mojang.com", L"https://bmclapi2.bangbang93.com" },
        { L"https://launcher.mojang.com", L"https://bmclapi2.bangbang93.com" },
        { L"https://libraries.minecraft.net", L"https://bmclapi2.bangbang93.com/maven" },
        { L"https://resources.download.minecraft.net", L"https://bmclapi2.bangbang93.com/assets" },
        { L"https://piston-data.mojang.com", L"https://bmclapi2.bangbang93.com" },
    };
    for (const auto& r : rules) {
        size_t n = wcslen(r.from);
        if (url.compare(0, n, r.from) == 0) return std::wstring(r.to) + url.substr(n);
    }
    return url;
}

bool ParseMcVersionJson(const std::string& json, McClientFile& out, std::string* err) {
    xgcfg::Value root;
    std::string e;
    if (!xgcfg::Parse(json, root, &e)) {
        if (err) *err = e;
        return false;
    }
    const xgcfg::Value* dl = root.FindPath("downloads.client");
    if (!dl || !dl->IsObject()) {
        if (err) *err = "version json 缺少 downloads.client";
        return false;
    }
    if (const xgcfg::Value* p = dl->Find("url")) out.url = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = dl->Find("sha1")) out.sha1 = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = dl->Find("size")) out.size = (long long)p->AsInt(0);
    return !out.url.empty();
}
