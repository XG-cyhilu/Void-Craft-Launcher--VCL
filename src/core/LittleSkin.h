#pragma once
#include <string>
#include <vector>

#include "core/Settings.h"

// LittleSkin（Yggdrasil 外置登录）用得到的常量与解析工具。
// 网络请求本身走 AsyncHttp，这里只放纯函数，方便单测与复用。
namespace ls {

inline const wchar_t* Origin() { return L"https://littleskin.cn"; }
inline const wchar_t* Yggdrasil() { return L"https://littleskin.cn/api/yggdrasil"; }
// 设备代码流（启动器这种公共客户端用它，不需要 client_secret）
inline const wchar_t* OpenOrigin() { return L"https://open.littleskin.cn"; }

// x-www-form-urlencoded 的值编码
std::wstring UrlEncode(const std::wstring& s);

// 顶层 JSON 字符串字段（没有就返回空）
std::wstring JsonStr(const std::string& json, const char* key);

// 顶层 JSON 数字字段（没有 / 类型不对就用 fallback）
long long JsonInt(const std::string& json, const char* key, long long fallback);

// Yggdrasil 登录响应 → 账户条目（取 selectedProfile，没有就取 availableProfiles 第一个）
bool ParseSession(const std::string& json, AccountEntry& out, std::wstring& err);

// 错误响应里的 errorMessage / error / message
std::wstring ErrorOf(const std::string& json, const std::wstring& fallback);

// OAuth 档案列表（GET /sessionserver/session/minecraft/profile）→ 第一个可用角色
bool ParseProfiles(const std::string& json, std::wstring& id, std::wstring& name, std::wstring& err);

} // namespace ls
