#pragma once
#include <string>

#include "core/Settings.h"

// 账户相关的纯函数：离线 UUID、名字校验、UUID 格式互转。
// 登录流程本身在外置登录客户端（LittleSkin）与账户页里。

// Java 离线 UUID：MD5("OfflinePlayer:" + 名字)，打 v3 / RFC variant 位，带横线输出。
std::wstring OfflineUuidOf(const std::wstring& name);

// 3–16 位，字母 / 数字 / 下划线（Minecraft 玩家名的离线可用子集）
bool ValidPlayerName(const std::wstring& name);

// 32 位无横线 ↔ 8-4-4-4-12
std::wstring DashUuid(const std::wstring& id32);
std::wstring UndashUuid(const std::wstring& dashed);

// 查找名字重复的账户（同类型同名字算重复）
int FindAccount(const Settings& s, const std::wstring& type, const std::wstring& name);

// 追加账户并设为当前；返回新账户下标
int AddAccount(Settings& s, AccountEntry e);

// 删除账户；当前账户被删时自动落到第一个
bool RemoveAccount(Settings& s, int index);

// 切换当前账户（同步 accountName，状态栏跟着变）
bool SetCurrentAccount(Settings& s, int index);

// 当前生效账户；列表为空返回 nullptr
const AccountEntry* CurrentAccount(const Settings& s);
