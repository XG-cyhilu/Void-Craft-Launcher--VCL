# Void Craft Launcher 架构设计（v0.1）

本文档是 VCL 重构的施工蓝图。所有实现以此为准；改动先改文档。

---

## 0. 定位

- **产品**：面向公开发布的 Minecraft Java 版启动器，C++17 + Direct2D 自绘。
- **兼容目标**：**目录结构与 PCL / 官方启动器完全一致**。任意一个标准 `.minecraft` 文件夹都能直接作为 Root 添加，扫到的版本立刻可启动。
- **原则**：
  1. 不污染游戏目录之外的东西（除 `versions\<name>\.vcl\` 这一个私有元数据目录）。
  2. 不复制任何 GPL 项目的实现代码，只按公开规范（Mojang version json、Fabric meta、Forge installer）自己实现。
  3. 加载器安装优先"外包给官方安装器"，不自行复刻 processors。

---

## 1. 技术栈与依赖

| 项 | 选型 |
|---|---|
| 标准 | C++17，MSVC x64 |
| 构建 | CMake ≥ 3.20，静态库 + install/export |
| UI | **VCUI**（自绘，Direct2D），`find_package(VCUI)` |
| 基础能力 | **cpploclib**：XGLOG / XGFS / XGCFG / XGDL / XGZIP / XGCRYPTO / XGPROC / XGNET / XGTHREAD |
| 网络 | XGNET（libcurl，Schannel） |
| 许可证 | AGPL-3.0（见仓库 LICENSE） |

依赖一律通过 `CMAKE_PREFIX_PATH` 提供安装前缀，不做 submodule 嵌套。

---

## 2. 目录与文件约定

### 2.1 Root（一个 Root = 一个标准 `.minecraft`）

```
<Root>
├── libraries\
├── assets\indexes\ , assets\objects\
├── versions\
├── launcher_profiles.json     ← Forge/NeoForge 安装器会校验它
└── PCL\ ...                   ← PCL 自己的东西，VCL 只读不写
```

Root 合法性判定：`versions\` 目录存在。**少于这个条件的文件夹不允许被添加。**

多个 Root 在 UI 里作为"游戏文件夹"列表管理，切换后重新扫描该 Root。

### 2.2 版本目录（一个版本 = 一个实例）

```
<Root>\versions\<folder>\
├── <folder>.json         ← 唯一识别依据（可能含 inheritsFrom）
├── <folder>.jar          ← 子版本可为空
├── mods\ config\ saves\ options.txt    ← 版本隔离时这些东西在这里
└── .vcl\                                ← VCL 私有，PCL 不会读
    ├── instance.json                    ← 本实例的启动器侧配置
    └── icon.png                         ← 可选，实例图标
```

**识别规则**：`versions` 下每个子目录里存在同名 `.json` 才算一个版本。中文/空格目录名照常支持。

### 2.3 gameDir（版本隔离）

每个实例有独立的 `gameDirMode`：

| 模式 | gameDir | 说明 |
|---|---|---|
| `VersionDir` | `<Root>\versions\<folder>\` | 版本隔离（PCL 默认隔离时的布局） |
| `RootDir` | `<Root>\` | 不隔离（mods 与所有版本共享） |
| `Custom` | 用户指定 | 高级需求 |

导入已有目录时的**启发式默认**：

- `versions\<folder>\` 下存在 `mods` / `config` / `saves` / `options.txt` 之一 → `VersionDir`
- 否则 → `RootDir`

UI 上必须能手动改，导入结果不做成"不可逆判断"。

### 2.4 全局配置与缓存（不写进游戏目录）

```
%APPDATA%\XGStudio\VoidCraftLauncher\global.json
%LOCALAPPDATA%\XGStudio\VoidCraftLauncher\
├── cache\natives\<rootHash>\<folder>\        ← natives 解压目标
├── cache\meta\                                ← version manifest / fabric profile 等缓存
├── cache\downloads\                           ← 临时下载
└── logs\<rootHash>\<folder>\launch-YYYYMMDD-HHMMSS.log
```

`rootHash` = Root 绝对路径（小写、正斜杠归一后）的 SHA-1 前 8 字节十六进制。

**natives 必须解到缓存目录，绝不写进版本目录**：多实例并发启动时不会互相覆盖 DLL。
**日志每实例一份**：用户出问题直接把 `logs\` 打包发出来。

---

## 3. 数据模型

### 3.1 `global.json`

```json
{
  "version": 1,
  "roots": [
    { "path": "D:\\Games\\MC-1.20", "name": "主目录" }
  ],
  "currentRoot": 0,
  "ui": { "language": "zh-CN", "theme": "auto", "opacity": 0.9 },
  "download": { "mirror": "mojang", "concurrency": 16 },
  "java": { "autoDetect": true, "preferredPath": "" }
}
```

### 3.2 `versions\<folder>\.vcl\instance.json`

```json
{
  "version": 1,
  "name": "1.20.4 Fabric",
  "javaPath": "",
  "memoryMb": 4096,
  "jvmArgs": ["-XX:+UseG1GC"],
  "gameDirMode": "VersionDir",
  "customGameDir": "",
  "loader": { "type": "fabric", "loaderVersion": "0.15.11", "installerVersion": "" },
  "createdAt": 1730000000,
  "lastPlayedAt": 1730000000,
  "totalPlayTimeSec": 0,
  "notes": ""
}
```

配置默认：每实例一份，**没有 Root 级"默认值继承"**（v0.1 不做，之后若需要再做一层覆盖）。
`instance.json` 缺失时按"全部默认 + 启发式 gameDir"即时生成，不阻断启动。

---

## 4. 模块划分与接口

```
src/
├── main.cpp
├── app/            App 生命周期、窗口、路由
├── core/           ★ 不依赖 UI、不依赖全局单例，可命令行跑通
│   ├── root_manager
│   ├── version_scanner
│   ├── version_resolver
│   ├── instance_store
│   ├── java_finder
│   ├── launcher
│   ├── loader_installer
│   ├── downloader
│   └── auth/       (v0.5)
└── ui/             VCUI 界面层，只消费 core 的数据
```

命名空间统一 `vcl`；目录小写下划线；每个头文件自带 include guard 风格一致的 `#pragma once`。

### 4.1 RootManager

```cpp
struct RootInfo { std::string path; std::string name; bool valid; };

class RootManager {
public:
    bool Load(const std::string& globalJsonPath);
    bool Save();
    const std::vector<RootInfo>& Roots() const;
    bool AddRoot(const std::string& path, std::string& err);   // 校验 versions\ 存在
    bool RemoveRoot(size_t index);
    int  CurrentIndex() const;
    bool SetCurrent(size_t index);
};
```

路径内部统一用 **UTF-8 `std::string`**（配合 XGFS），只在 Win32 API 边界转宽字符。

### 4.2 VersionScanner

```cpp
struct VersionEntry {
    std::string folder;      // 目录名
    std::string id;          // json 内 "id"，可能不同于 folder
    std::string jsonPath;
    std::string jarPath;     // 可为空
    std::string type;        // release / snapshot / old_beta / old_alpha
    std::string loader;      // vanilla / fabric / quilt / forge / neoforge
    std::string baseVersion; // inheritsFrom 链顶端 = 原版版本号
};

class VersionScanner {
public:
    std::vector<VersionEntry> Scan(const std::string& rootDir) const;
};
```

`loader` 探测：读 json 的 `mainClass` / `libraries` 中的 `net.fabricmc`、`net.minecraftforge`、`net.neoforged`、`org.quiltmc` 关键字。

### 4.3 VersionResolver

```cpp
struct ResolvedVersion {
    std::string id, mainClass, jarPath, assetIndexId, assetIndexUrl;
    std::vector<std::string> classpath;
    std::vector<std::string> jvmArgs, gameArgs;   // 已展开 rules，未做变量替换
};

class VersionResolver {
public:
    // 递归合并 inheritsFrom（深度上限 10），求值 rules（os/arch/features），
    // 过滤库、补 natives、生成 classpath（顺序：自身 → 父 → 更深父）
    bool Resolve(const std::string& rootDir, const VersionEntry& e,
                 ResolvedVersion& out, std::string& err) const;
};
```

要点：
- `libraries` 按 rules 过滤，`natives` 段按当前 OS 取 classifier，解压到缓存 natives 目录（`XGZIP`，`extract.exclude` 要遵守）。
- 子版本 **没有自己的 jar** 时用父版本的 jar。
- `arguments` 新旧两代格式都要支持（`minecraftArguments` 老格式拼字符串）。
- 变量替换：`${auth_player_name}` `${version_name}` `${game_directory}` `${assets_root}` `${assets_index_name}` `${auth_uuid}` `${auth_access_token}` `${user_type}` `${version_type}` `${natives_directory}` `${launcher_name}` `${launcher_version}` `${classpath}` `${classpath_separator}`。

### 4.4 JavaFinder

```cpp
struct JavaInfo { std::string path; int major; bool x64; bool ok; };

class JavaFinder {
public:
    std::vector<JavaInfo> ScanCached();          // 结果缓存进 cache\java.json
    bool ReadRelease(const std::string& javaHome, JavaInfo& out) const; // 读 release 文件
};
```

探测来源：实例配置指定 → `JAVA_HOME` → 注册表 `SOFTWARE\JavaSoft` / `JavaSoft\JDK` → PATH → 常见目录（`C:\Program Files\Java`、`Eclipse Adoptium`、`Microsoft\jdk`、`Zulu` 等）。
位数判定：PE 头 `Machine` 字段（0x8664 = x64）。
版本需求：Mojang 的 `javaVersion.majorVersion` 优先，缺失时按版本号推测。**不匹配要明确提示，不静默用错版本。**

### 4.5 Launcher

```cpp
struct LaunchOptions {
    std::string playerName, uuid, accessToken, userType;  // 离线：uuid 由名字派生
    int memoryMb = 4096;
    std::vector<std::string> extraJvmArgs;
    std::string javaPath;      // 空 = 自动
    std::string serverAddress; // 空 = 不进服
    int width = 0, height = 0;
    bool demo = false;
};

class Launcher {
public:
    bool BuildCommand(const ResolvedVersion&, const LaunchOptions&,
                      std::string& cmdLine, std::string& gameDir, std::string& err) const;
    // 起进程：XGPROC（Job 对象保证关闭启动器时一起收掉 JVM）
};
```

### 4.6 LoaderInstaller

| 加载器 | 做法 |
|---|---|
| Fabric / Quilt | `GET meta.fabricmc.net/v2/versions/loader/<mc>/<loader>/profile/json` → 补 `id` → 写 `versions\<id>\<id>.json` + `.vcl\instance.json`。**不需要 JVM。** |
| Forge | `GET maven.minecraftforge.net/.../forge-<mc>-<ver>-installer.jar` → `java -jar <jar> --installClient <root>` |
| NeoForge | 同 Forge，源换 `maven.neoforged.net` |

Forge/NeoForge 要点：
- 调用前确保 `<root>\launcher_profiles.json` 存在（缺失时写最小骨架 `{"profiles":{},"settings":{},"version":3}`），否则安装器报错。
- 输出用 `XGPROC` 逐行抓取，映射成进度（"Downloading libraries" 等关键词），不要只给个转圈。
- 安装完重扫 `versions`，把新出现的版本建为实例（复用原实例的 javaPath / 内存设置）。
- 安装器要求目标实例是 vanilla 版本；对"已是 Forge"的版本不重复安装。

### 4.7 Downloader

基于 XGDL 的并发层：

1. `version_manifest_v2.json` → 缓存到 `cache\meta`
2. 版本 json + client jar（`downloads.client` 的 url + sha1）
3. `libraries`（按 rules 过滤后的 artifact + natives classifier）
4. `assetIndex.url` → `objects\<前2位>\<hash>`，**全量下载**，并发 16，失败重试 3 次
5. 全部完成后用 sha1 校验；失败文件重下

进度上报用 `atomic` 计数（XGTHREAD），UI 显示 "已完成/总数" 而非仅百分比——两三万个小文件，纯百分比看起来像卡死。

> **待办**：确认 `XGCRYPTO` 是否提供 SHA-1；Mojang 校验全用 sha1，没有就补一个 `Sha1` 算法。

---

## 5. 启动流程

```
1. 载入 global.json → 选中 Root
2. 扫 versions → 选中 VersionEntry
3. 读 .vcl\instance.json（缺失则默认）
4. VersionResolver：合并 inheritsFrom、求值 rules、解 natives、拼 classpath
5. JavaFinder 选 JDK（校验 major 与 javaVersion.majorVersion）
6. 拼参数（JVM → mainClass → game args，变量替换）
7. 确保 gameDir / natives 目录存在
8. XGPROC 启动（工作目录 = gameDir，Job 对象托管）
9. 逐行读 stdout/stderr → XGLOG（每实例一个文件）+ UI 日志面板
10. 退出码采集 → 写回 instance.json 的 lastPlayedAt / totalPlayTimeSec
```

崩溃判定：退出码非 0 且 stderr 出现 `Exception`/`Error` → UI 直接给"查看日志 / 打开目录"按钮。

---

## 6. 里程碑

| 版本 | 内容 |
|---|---|
| v0.1 | Root 添加/扫描、版本列表、instance.json、vanilla 启动、最简 UI、离线登录、每实例日志 |
| v0.2 | Fabric / Quilt 安装（纯 JSON） |
| v0.3 | Forge / NeoForge 安装（调 installer jar） |
| v0.4 | 下载器完善（原版安装）、多 Root 管理 UI、设置页 |
| v0.5 | 微软登录（公开发布的前置条件） |
| v0.6 | 崩溃日志包、更新检查、Modrinth |

## 7. v0.1 任务清单

- [ ] CMakeLists：链接 VCUI + 9 个 cpploclib 模块，`CMAKE_PREFIX_PATH` 约定
- [ ] `RootManager` + `global.json` 读写（XGCFG）
- [ ] `VersionScanner`（扫 versions，探 loader）
- [ ] `InstanceStore`（`.vcl\instance.json` 读写 + 默认值 + 启发式 gameDir）
- [ ] `VersionResolver`（inheritsFrom / rules / classpath / natives）
- [ ] `JavaFinder`（release 文件 + 注册表 + 常见目录）
- [ ] `Launcher`（参数拼装 + XGPROC 起进程 + 日志分流）
- [ ] UI：Root 选择页、版本列表页、启动页（进度/日志）、设置页骨架
- [ ] 离线 UUID：`OfflineUUID(name)`（UUID v3，等价 Java 的 `nameUUIDFromBytes("OfflinePlayer:"+name)`）

## 8. 编码规范

- 源码 UTF-8 **带 BOM**（外部 MSVC 不加 `/utf-8` 时按 936 解析会炸）。
- 编译选项 `/utf-8 /W3`，`cxx_std_17`。
- 错误传递用 `bool + std::string& err`（不抛异常穿越模块边界）。
- 所有对外文件路径参数用 UTF-8 `std::string`。
- 每个文件 Apache/AGPL 头 + SPDX（本仓库统一 AGPL-3.0）。
