#include "core/Res.h"

#include <windows.h>

namespace vcl {

namespace {

wchar_t UpperAscii(wchar_t c) {
    if (c >= L'a' && c <= L'z') return (wchar_t)(c - L'a' + L'A');
    return c;
}

} // namespace

// 与 CMakeLists 里生成 .rc 的规则保持一致：大写 + 非字母数字一律换成下划线。
std::wstring EmbeddedName(const std::wstring& rel) {
    std::wstring n = L"R_";
    n.reserve(rel.size() + 2);
    for (wchar_t c : rel) {
        const wchar_t u = UpperAscii(c);
        const bool ok = (u >= L'A' && u <= L'Z') || (u >= L'0' && u <= L'9');
        n.push_back(ok ? u : L'_');
    }
    return n;
}

bool EmbeddedResource(const std::wstring& rel, const void** data, unsigned* size) {
    if (data) *data = nullptr;
    if (size) *size = 0;
    if (rel.empty()) return false;

    const std::wstring name = EmbeddedName(rel);
    HRSRC res = FindResourceW(nullptr, name.c_str(), RT_RCDATA);
    if (!res) return false;
    HGLOBAL glob = LoadResource(nullptr, res);
    if (!glob) return false;
    const void* p = LockResource(glob);
    if (!p) return false;
    const DWORD n = SizeofResource(nullptr, res);
    if (n == 0) return false;
    if (data) *data = p;
    if (size) *size = (unsigned)n;
    return true;
}

} // namespace vcl
