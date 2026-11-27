#include "core/LittleSkin.h"

#include "core/Accounts.h"
#include "core/Text.h"
#include "xgcfg/xgcfg.h"

namespace ls {

std::wstring UrlEncode(const std::wstring& s) {
    std::wstring out;
    const std::string utf8 = WideToUtf8(s);
    for (unsigned char b : utf8) {
        if ((b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || b == '-' ||
            b == '_' || b == '.' || b == '~') {
            out.push_back((wchar_t)b);
        } else {
            wchar_t buf[8];
            swprintf_s(buf, L"%%%02X", b);
            out += buf;
        }
    }
    return out;
}

namespace {

bool ParseJson(const std::string& json, xgcfg::Value& root) {
    std::string err;
    return xgcfg::Parse(json, root, &err, true) && root.IsObject();
}

std::wstring StrOf(const xgcfg::Value& obj, const char* key) {
    const xgcfg::Value* v = obj.Find(key);
    if (!v || !v->IsString()) return {};
    return Utf8ToWide(v->AsString());
}

} // namespace

std::wstring JsonStr(const std::string& json, const char* key) {
    xgcfg::Value root;
    if (!ParseJson(json, root)) return {};
    return StrOf(root, key);
}

bool ParseSession(const std::string& json, AccountEntry& out, std::wstring& err) {
    xgcfg::Value root;
    if (!ParseJson(json, root)) {
        err = L"登录响应不是 JSON";
        return false;
    }
    const xgcfg::Value* sp = root.Find("selectedProfile");
    if ((!sp || !sp->IsObject())) {
        if (const xgcfg::Value* av = root.Find("availableProfiles"); av && av->IsArray() && av->Size() > 0)
            sp = &av->At(0);
    }
    if (!sp || !sp->IsObject()) {
        err = ErrorOf(json, L"登录失败：响应里没有可用角色");
        return false;
    }
    out.type = L"littleskin";
    out.name = StrOf(*sp, "name");
    out.uuid = UndashUuid(StrOf(*sp, "id"));
    out.accessToken = StrOf(root, "accessToken");
    out.clientToken = StrOf(root, "clientToken");
    if (out.name.empty() || out.uuid.empty() || out.accessToken.empty()) {
        err = L"登录失败：角色信息不完整";
        return false;
    }
    return true;
}

std::wstring ErrorOf(const std::string& json, const std::wstring& fallback) {
    xgcfg::Value root;
    if (!ParseJson(json, root)) return fallback;
    std::wstring m = StrOf(root, "errorMessage");
    if (m.empty()) m = StrOf(root, "message");
    if (m.empty()) m = StrOf(root, "error");
    if (m.empty()) m = StrOf(root, "error_description");
    return m.empty() ? fallback : m;
}

bool ParseProfiles(const std::string& json, std::wstring& id, std::wstring& name, std::wstring& err) {
    xgcfg::Value root;
    std::string perr;
    if (!xgcfg::Parse(json, root, &perr, true) || !root.IsArray()) {
        err = L"读取角色列表失败";
        return false;
    }
    if (root.Size() == 0) {
        err = L"这个账户下还没有角色，先去 LittleSkin 建一个角色";
        return false;
    }
    const xgcfg::Value& first = root.At(0);
    if (!first.IsObject()) {
        err = L"角色列表格式异常";
        return false;
    }
    id = UndashUuid(StrOf(first, "id"));
    name = StrOf(first, "name");
    if (id.empty() || name.empty()) {
        err = L"角色信息不完整";
        return false;
    }
    return true;
}

} // namespace ls
