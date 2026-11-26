#pragma once
#include <functional>
#include <string>
#include <vector>

#include "core/AsyncHttp.h"

// 只收录 x64 + Windows + zip + GA 的 LTS 版本；Oracle 不在列表里。
struct JavaVendorDef {
    std::wstring id;        // foojay 的 distribution 标识
    std::wstring name;      // 展示名
    std::wstring shortName; // 徽标用的两三个字
};

const std::vector<JavaVendorDef>& JavaVendors();

struct JavaRelease {
    std::wstring vendor;        // foojay id
    std::wstring vendorName;    // 展示名
    int major = 0;              // 21
    std::wstring version;       // 21.0.13.1
    std::wstring filename;      // microsoft-jdk-21.0.13.1-windows-x64.zip
    std::wstring url;           // 下载地址（foojay 的 redirect）
    long long size = 0;
    bool jdk = true;
};

// 按厂商拉 LTS 版本列表（动态：走 api.foojay.io/disco/v3.0/packages）
void JavaFetchMajors(AsyncHttp* http, const std::wstring& vendorId,
                     std::function<void(bool ok, std::vector<int> majors, const std::wstring& err)> cb);

// 拉某个厂商 + 某个 LTS 主版本的最新包（JDK 或 JRE）
void JavaFetchRelease(AsyncHttp* http, const std::wstring& vendorId, int major, bool jdk,
                      std::function<void(bool ok, const JavaRelease& rel, const std::wstring& err)> cb);

// 这个厂商有没有单独的 JRE 包（Microsoft 只有 JDK）
bool JavaVendorHasJre(const std::wstring& vendorId);
