#pragma once
#include <string>
#include <vector>

// 一个已安装的 Java 运行时（JDK 或 JRE）的只读描述。
struct JavaRuntime {
    std::wstring home;        // JAVA_HOME 目录，结尾不带分隔符
    std::wstring exe;         // 启动用可执行文件：优先 javaw.exe
    std::wstring version;     // release 文件里的 JAVA_VERSION，"21.0.5"
    int major = 0;            // 主版本号：8 / 11 / 17 / 21 / 25
    std::wstring vendor;      // 展示用发行商名："Eclipse Temurin"
    std::wstring implementor; // release 里的原始 IMPLEMENTOR
    bool jdk = false;         // 有 bin\javac.exe 且 lib\modules 之外还有开发组件
    bool x64 = true;          // 64 位（按 PE 头判断，其次看 release 的 OS_ARCH）
    bool lts = false;
    std::wstring source;      // 来源：注册表 / PATH / 常见目录 / 手动 / 已下载
};

// 从 java.exe / javaw.exe 路径或 JAVA_HOME 目录读出一个运行时的信息。
// 主要靠 <home>\release，缺这个文件时退回跑一次 java -version 解析。
bool JavaInspect(const std::wstring& fileOrHome, JavaRuntime& out,
                 const std::wstring& source = std::wstring());

// 只扫便宜的地方：PATH 里每个目录、JAVA_HOME / JDK_HOME 系列环境变量、
// 启动器自己的 java 目录；extraPaths 是用户手动加过、配置里记下来的路径。
// （不去翻 Program Files，那棵树太大，扫一次会把 UI 卡住。）
std::vector<JavaRuntime> JavaScan(const std::vector<std::wstring>& extraPaths = {});

// 启动器自己下载安装 Java 的目录：%LOCALAPPDATA%\XGStudio\VoidCraftLauncher\java
std::wstring JavaInstallDir();

// "21.0.5+11-LTS" / "1.8.0_442" → 主版本号；失败返回 0
int JavaMajorOf(const std::wstring& version);

// LTS 判定：8 / 11 / 17 / 21 / 25 或版本串里带 LTS
bool JavaIsLts(int major, const std::wstring& version);

// 版本比较（major 相同时比较 minor/patch），a 新于 b 返回 > 0
int JavaVersionCompare(const JavaRuntime& a, const JavaRuntime& b);
