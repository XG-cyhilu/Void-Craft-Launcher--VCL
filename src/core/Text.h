#pragma once
#include <string>

// UTF-8 <-> UTF-16 以及界面上用到的几个格式化小工具。
// 项目约定：对外路径/接口一律 UTF-8 std::string，界面（VCUI）一律 std::wstring。

std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

// printf 风格
std::wstring WFormat(const wchar_t* fmt, ...);

// 1234567 -> "1.2 MB"（1024 进制，保留一位）
std::wstring HumanBytes(long long bytes);

// 2026-11-24T12:03:04+00:00 -> 2026-11-24
std::wstring IsoDateOnly(const std::wstring& iso);

std::wstring ToLower(const std::wstring& s);
std::wstring Trim(const std::wstring& s);
