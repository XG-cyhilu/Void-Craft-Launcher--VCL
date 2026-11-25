#include "core/ModrinthApi.h"
#include "core/Text.h"

#include "xgcfg/xgcfg.h"

static std::string UrlEncode(const std::wstring& utf8ish) {
    std::string s = WideToUtf8(utf8ish);
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            char buf[4];
            sprintf_s(buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::wstring MrTypeName(const std::wstring& t) {
    if (t == L"mod") return L"模组";
    if (t == L"modpack") return L"整合包";
    if (t == L"resourcepack") return L"资源包";
    if (t == L"shader") return L"光影包";
    if (t == L"datapack") return L"数据包";
    return t;
}

std::wstring MrTargetFolder(const std::wstring& t) {
    if (t == L"mod") return L"mods";
    if (t == L"modpack") return L"modpacks";
    if (t == L"resourcepack") return L"resourcepacks";
    if (t == L"shader") return L"shaderpacks";
    if (t == L"datapack") return L"datapacks";
    return L"mods";
}

std::wstring MrSearchUrl(const std::wstring& projectType, const std::wstring& query,
                         const std::wstring& index, int limit, int offset) {
    std::wstring facets = L"[[\"project_type:" + projectType + L"\"]]";
    std::wstring url = L"https://api.modrinth.com/v2/search?query=";
    url += Utf8ToWide(UrlEncode(query));
    url += L"&limit=" + std::to_wstring(limit);
    url += L"&offset=" + std::to_wstring(offset);
    url += L"&index=" + (index.empty() ? std::wstring(L"relevance") : index);
    url += L"&facets=" + Utf8ToWide(UrlEncode(facets));
    return url;
}

bool ParseMrSearch(const std::string& json, std::vector<MrHit>& out, std::string* err,
                   long long* totalHits) {
    xgcfg::Value root;
    std::string e;
    if (!xgcfg::Parse(json, root, &e)) {
        if (err) *err = e;
        return false;
    }
    const xgcfg::Value* hits = root.Find("hits");
    if (!hits || !hits->IsArray()) {
        if (err) *err = "search 响应缺少 hits";
        return false;
    }
    if (totalHits) {
        const xgcfg::Value* t = root.Find("total_hits");
        *totalHits = (t && t->IsNumber()) ? (long long)t->AsInt(0) : 0;
    }
    out.clear();
    for (size_t i = 0; i < hits->Size(); ++i) {
        const xgcfg::Value& h = hits->At(i);
        MrHit m;
        if (const xgcfg::Value* p = h.Find("project_id")) m.id = Utf8ToWide(p->AsString());
        if (const xgcfg::Value* p = h.Find("slug")) m.slug = Utf8ToWide(p->AsString());
        if (const xgcfg::Value* p = h.Find("title")) m.title = Utf8ToWide(p->AsString());
        if (const xgcfg::Value* p = h.Find("description")) m.description = Utf8ToWide(p->AsString());
        if (const xgcfg::Value* p = h.Find("author")) m.author = Utf8ToWide(p->AsString());
        if (const xgcfg::Value* p = h.Find("project_type")) m.projectType = Utf8ToWide(p->AsString());
        if (const xgcfg::Value* p = h.Find("icon_url")) m.iconUrl = Utf8ToWide(p->AsString());
        if (const xgcfg::Value* p = h.Find("downloads")) m.downloads = (long long)p->AsInt(0);
        if (const xgcfg::Value* p = h.Find("follows")) m.follows = (long long)p->AsInt(0);
        if (const xgcfg::Value* p = h.Find("date_modified")) m.updated = Utf8ToWide(p->AsString());
        if (m.id.empty()) continue;
        out.push_back(std::move(m));
    }
    return true;
}

std::wstring MrProjectUrl(const std::wstring& idOrSlug) {
    return L"https://api.modrinth.com/v2/project/" + idOrSlug;
}

bool ParseMrProject(const std::string& json, MrProject& out, std::string* err) {
    xgcfg::Value root;
    std::string e;
    if (!xgcfg::Parse(json, root, &e)) {
        if (err) *err = e;
        return false;
    }
    if (!root.IsObject()) {
        if (err) *err = "project 响应不是对象";
        return false;
    }
    out = MrProject{};
    if (const xgcfg::Value* p = root.Find("id")) out.id = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = root.Find("slug")) out.slug = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = root.Find("title")) out.title = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = root.Find("description")) out.description = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = root.Find("body")) out.bodyMd = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = root.Find("icon_url")) out.iconUrl = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = root.Find("source_url")) out.sourceUrl = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = root.Find("issues_url")) out.issuesUrl = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = root.Find("client_side")) out.clientSide = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = root.Find("server_side")) out.serverSide = Utf8ToWide(p->AsString());
    if (const xgcfg::Value* p = root.Find("downloads")) out.downloads = (long long)p->AsInt(0);
    if (const xgcfg::Value* p = root.Find("followers")) out.follows = (long long)p->AsInt(0);
    if (const xgcfg::Value* p = root.Find("license")) {
        if (const xgcfg::Value* id = p->Find("id")) out.license = Utf8ToWide(id->AsString());
    }
    auto readArr = [&root](const char* key, std::vector<std::wstring>& dst) {
        if (const xgcfg::Value* p = root.Find(key); p && p->IsArray())
            for (size_t i = 0; i < p->Size(); ++i) dst.push_back(Utf8ToWide(p->At(i).AsString()));
    };
    readArr("categories", out.categories);
    readArr("game_versions", out.gameVersions);
    if (out.id.empty() && out.slug.empty()) {
        if (err) *err = "project 响应缺少 id/slug";
        return false;
    }
    return true;
}

std::wstring MrVersionsUrl(const std::wstring& projectId) {
    return L"https://api.modrinth.com/v2/project/" + projectId + L"/version";
}

bool ParseMrVersions(const std::string& json, std::vector<MrFile>& out, std::string* err) {
    xgcfg::Value root;
    std::string e;
    if (!xgcfg::Parse(json, root, &e)) {
        if (err) *err = e;
        return false;
    }
    if (!root.IsArray()) {
        if (err) *err = "version 响应不是数组";
        return false;
    }
    out.clear();
    for (size_t i = 0; i < root.Size(); ++i) {
        const xgcfg::Value& v = root.At(i);
        std::wstring vname;
        if (const xgcfg::Value* p = v.Find("version_number")) vname = Utf8ToWide(p->AsString());
        else if (const xgcfg::Value* p2 = v.Find("name")) vname = Utf8ToWide(p2->AsString());

        std::wstring games, loaders;
        if (const xgcfg::Value* p = v.Find("game_versions"); p && p->IsArray()) {
            for (size_t k = 0; k < p->Size(); ++k) {
                if (k) games += L", ";
                games += Utf8ToWide(p->At(k).AsString());
            }
        }
        if (const xgcfg::Value* p = v.Find("loaders"); p && p->IsArray()) {
            for (size_t k = 0; k < p->Size(); ++k) {
                if (k) loaders += L", ";
                loaders += Utf8ToWide(p->At(k).AsString());
            }
        }

        const xgcfg::Value* files = v.Find("files");
        if (!files || !files->IsArray()) continue;
        for (size_t k = 0; k < files->Size(); ++k) {
            const xgcfg::Value& f = files->At(k);
            MrFile mf;
            mf.versionName = vname;
            mf.gameVersions = games;
            mf.loaders = loaders;
            if (const xgcfg::Value* p = f.Find("filename")) mf.name = Utf8ToWide(p->AsString());
            if (const xgcfg::Value* p = f.Find("url")) mf.url = Utf8ToWide(p->AsString());
            if (const xgcfg::Value* p = f.Find("size")) mf.size = (long long)p->AsInt(0);
            if (const xgcfg::Value* p = f.Find("primary")) mf.primary = p->AsBool(false);
            if (const xgcfg::Value* h = f.Find("hashes")) {
                if (const xgcfg::Value* p = h->Find("sha1")) mf.sha1 = Utf8ToWide(p->AsString());
            }
            if (mf.url.empty()) continue;
            out.push_back(std::move(mf));
        }
    }
    return true;
}

