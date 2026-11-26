#include "core/JavaCatalog.h"
#include "core/JavaRuntime.h"
#include "core/Text.h"

#include "xgcfg/xgcfg.h"

#include <algorithm>

namespace {

const std::vector<JavaVendorDef> kVendors = {
    { L"temurin",   L"Eclipse Temurin",    L"Temurin" },
    { L"microsoft", L"Microsoft OpenJDK",  L"Microsoft" },
    { L"zulu",      L"Azul Zulu",          L"Zulu" },
    { L"liberica",  L"BellSoft Liberica",  L"Liberica" },
    { L"corretto",  L"Amazon Corretto",    L"Corretto" },
    { L"semeru",    L"IBM Semeru",         L"Semeru" },
    { L"dragonwell", L"Alibaba Dragonwell", L"Dragonwell" },
};

std::wstring BaseQuery(const std::wstring& vendor, bool jdk) {
    std::wstring url = L"https://api.foojay.io/disco/v3.0/packages?distribution=";
    url += vendor;
    url += L"&operating_system=windows&architecture=x64&archive_type=zip&release_status=ga";
    url += L"&package_type=" + std::wstring(jdk ? L"jdk" : L"jre");
    url += L"&directly_downloadable=true";
    return url;
}

std::wstring NameOf(const std::wstring& vendorId) {
    for (const auto& v : kVendors)
        if (v.id == vendorId) return v.name;
    return vendorId;
}

// 同一批结果里可能有多个 build，挑 java_version 最大的那个
bool PickBest(const std::string& json, JavaRelease& out, std::wstring& err) {
    xgcfg::Value root;
    std::string e;
    if (!xgcfg::Parse(json, root, &e)) { err = Utf8ToWide(e.empty() ? "响应不是合法 JSON" : e); return false; }
    const xgcfg::Value* res = root.Find("result");
    if (!res || !res->IsArray()) { err = L"接口没有返回 result 列表"; return false; }

    const xgcfg::Value* best = nullptr;
    for (size_t i = 0; i < res->Size(); ++i) {
        const xgcfg::Value& it = res->At(i);
        const xgcfg::Value* v = it.Find("java_version");
        if (!v || !v->IsString()) continue;
        std::wstring ver = Utf8ToWide(v->AsString());
        if (!best) { best = &it; continue; }
        JavaRuntime a, b;
        a.version = ver;
        b.version = Utf8ToWide(best->Find("java_version")->AsString());
        a.major = JavaMajorOf(ver);
        b.major = JavaMajorOf(b.version);
        if (JavaVersionCompare(a, b) > 0) best = &it;
    }
    if (!best) { err = L"没有可下载的包"; return false; }

    auto numOf = [](const xgcfg::Value* n) { return (n && n->IsNumber()) ? (long long)n->AsInt(0) : 0LL; };
    auto strOf = [](const xgcfg::Value* n) { return (n && n->IsString()) ? Utf8ToWide(n->AsString()) : std::wstring(); };

    if (const xgcfg::Value* v = best->Find("java_version")) out.version = Utf8ToWide(v->AsString());
    out.major = JavaMajorOf(out.version);
    if (const xgcfg::Value* v = best->Find("size")) out.size = numOf(v);
    if (const xgcfg::Value* v = best->Find("filename")) out.filename = strOf(v);
    if (const xgcfg::Value* links = best->Find("links")) {
        out.url = strOf(links->Find("pkg_download_redirect"));
        if (out.url.empty()) out.url = strOf(links->Find("download_redirect"));
    }
    if (out.url.empty()) { err = L"接口没有给出下载地址"; return false; }
    return true;
}

} // namespace

const std::vector<JavaVendorDef>& JavaVendors() { return kVendors; }

bool JavaVendorHasJre(const std::wstring& vendorId) {
    return vendorId != L"microsoft" && vendorId != L"dragonwell";
}

void JavaFetchMajors(AsyncHttp* http, const std::wstring& vendorId,
                     std::function<void(bool, std::vector<int>, const std::wstring&)> cb) {
    if (!http) return;
    std::wstring url = BaseQuery(vendorId, true) + L"&latest=available&page_size=60";
    http->Get(url, [vendorId, cb](bool ok, const std::string& body, const std::string& err) {
        if (!ok) { cb(false, {}, Utf8ToWide(err)); return; }
        xgcfg::Value root;
        std::string e;
        if (!xgcfg::Parse(body, root, &e)) { cb(false, {}, L"版本列表解析失败"); return; }
        const xgcfg::Value* res = root.Find("result");
        if (!res || !res->IsArray()) { cb(false, {}, L"版本列表为空"); return; }

        std::vector<int> majors;
        for (size_t i = 0; i < res->Size(); ++i) {
            const xgcfg::Value& it = res->At(i);
            const xgcfg::Value* jv = it.Find("jdk_version");
            if (!jv || !jv->IsNumber()) continue;
            int major = (int)jv->AsInt(0);
            const xgcfg::Value* ts = it.Find("term_of_support");
            bool lts = (ts && ts->IsString()) ? ToLower(Utf8ToWide(ts->AsString())) == L"lts"
                                              : JavaIsLts(major, L"");
            if (!lts) continue;
            if (std::find(majors.begin(), majors.end(), major) == majors.end()) majors.push_back(major);
        }
        if (majors.empty())
            for (int m : { 8, 11, 17, 21, 25 }) majors.push_back(m);   // 接口抽风时的兜底
        std::sort(majors.begin(), majors.end(), std::greater<int>());
        cb(true, majors, L"");
    });
}

void JavaFetchRelease(AsyncHttp* http, const std::wstring& vendorId, int major, bool jdk,
                      std::function<void(bool, const JavaRelease&, const std::wstring&)> cb) {
    if (!http) return;
    std::wstring url = BaseQuery(vendorId, jdk) + L"&version=" + std::to_wstring(major) +
                       L"&latest=available&page_size=20";
    http->Get(url, [vendorId, jdk, cb](bool ok, const std::string& body, const std::string& err) {
        if (!ok) { cb(false, {}, Utf8ToWide(err)); return; }
        JavaRelease rel;
        rel.vendor = vendorId;
        rel.vendorName = NameOf(vendorId);
        rel.jdk = jdk;
        std::wstring e;
        if (!PickBest(body, rel, e)) { cb(false, {}, e.empty() ? L"没有可下载的包" : e); return; }
        cb(true, rel, L"");
    });
}
