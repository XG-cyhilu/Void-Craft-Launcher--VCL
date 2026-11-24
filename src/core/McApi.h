#pragma once
#include <string>
#include <vector>

struct McVersion {
    std::wstring id;
    std::wstring type;          // release / snapshot / old_beta / old_alpha
    std::wstring releaseTime;   // ISO
    std::wstring url;           // 版本 json 地址
};

struct McClientFile {
    std::wstring url;
    std::wstring sha1;
    long long size = 0;
};

std::wstring McManifestUrl(bool bmclapi);
bool ParseMcManifest(const std::string& json, std::vector<McVersion>& out, std::string* err);

// 把 Mojang 官方域名换到 BMCLAPI 镜像（bmclapi=false 时原样返回）
std::wstring MirrorUrl(const std::wstring& url, bool bmclapi);

// version json 里 downloads.client 的信息（v0.1 只装 json + client.jar）
bool ParseMcVersionJson(const std::string& json, McClientFile& out, std::string* err);
