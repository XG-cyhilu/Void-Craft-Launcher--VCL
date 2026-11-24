#include "core/Text.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cwctype>

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring WFormat(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    return std::wstring(buf);
}

std::wstring HumanBytes(long long bytes) {
    if (bytes < 0) return L"—";
    double v = (double)bytes;
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    if (u == 0) return WFormat(L"%lld B", bytes);
    return WFormat(L"%.1f %s", v, units[u]);
}

std::wstring IsoDateOnly(const std::wstring& iso) {
    size_t t = iso.find(L'T');
    return t == std::wstring::npos ? iso : iso.substr(0, t);
}

std::wstring ToLower(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out) c = (wchar_t)towlower(c);
    return out;
}

std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}
