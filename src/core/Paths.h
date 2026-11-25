#pragma once
#include <string>

// 配置与缓存的落盘位置（见 docs/ARCHITECTURE.md §2.5）
//   %APPDATA%\XGStudio\VoidCraftLauncher\global.json
//   %LOCALAPPDATA%\XGStudio\VoidCraftLauncher\cache\...
std::wstring AppConfigDir();
std::wstring AppCacheDir();
std::wstring GlobalConfigPath();

// 会话级缓存（关启动器就清）：JSON 响应与图片各占一层
std::wstring HttpCacheDir();
std::wstring IconCacheDir();

// 程序自带的 resource 目录（exe 同级的 resource；开发时回退到源码树）
std::wstring ResourceDir();

std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
void EnsureDir(const std::wstring& dir);
bool DirExists(const std::wstring& dir);
bool FileExists(const std::wstring& file);
// 递归删除目录里的内容（目录本身留着），用于清会话缓存
void ClearTree(const std::wstring& dir);

// 系统默认的 .minecraft 位置；不存在也照样返回（下载时会按需建子目录）
std::wstring DefaultRootPath();

// 把相对/带引号的用户输入整理成绝对路径
std::wstring NormalizePath(const std::wstring& in);

