#pragma once
#include <string>
#include <vector>

// Modrinth v2。除"游戏"以外的所有下载类别都只走这一个源。
// 注意：Modrinth 要求带一个能识别应用的 User-Agent（见 AsyncHttp 默认 UA）。

struct MrHit {
    std::wstring id;            // project_id
    std::wstring slug;
    std::wstring title;
    std::wstring description;
    std::wstring author;
    std::wstring projectType;
    long long downloads = 0;
    long long follows = 0;
};

struct MrFile {
    std::wstring name;          // 文件名
    std::wstring url;
    std::wstring sha1;
    long long size = 0;
    std::wstring versionName;   // 版本号/名称
    std::wstring gameVersions;  // "1.20.1, 1.20.2"
    std::wstring loaders;       // "fabric, quilt"
    bool primary = false;
};

// project_type: mod / modpack / resourcepack / shader / datapack
std::wstring MrSearchUrl(const std::wstring& projectType, const std::wstring& query,
                         const std::wstring& index, int limit = 24, int offset = 0);
bool ParseMrSearch(const std::string& json, std::vector<MrHit>& out, std::string* err);

std::wstring MrVersionsUrl(const std::wstring& projectId);
bool ParseMrVersions(const std::string& json, std::vector<MrFile>& out, std::string* err);

// 各类下载物在 Root 下的目标子目录
std::wstring MrTargetFolder(const std::wstring& projectType);

// 给界面用的中文类型名
std::wstring MrTypeName(const std::wstring& projectType);
