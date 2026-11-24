# Void Craft Launcher 架构设计（v0.2）

本文档是 VCL 重构的施工蓝图。所有实现以此为准；改动先改文档。

> **v0.2（2026-11-24）**：依赖由 `find_package` 改为**源码内置**（VCUI、cpploclib 复制进仓库）；
> **启动与 mod loader 安装外包给 Rust 库 LightyLauncherLib**，不再自研 `VersionResolver` / `Launcher` /
> `LoaderInstaller`；运行时统一 `/MD`。v0.1 的实施顺序改为**先立 UI 骨架**。

---

## 0. 定位

- **产品**：面向公开发布的 Minecraft Java 版启动器，C++17 + Direct2D 自绘。
- **兼容目标**：**目录结构与 PCL / 官方启动器完全一致**。任意一个标准 `.minecraft` 文件夹都能直接作为 Root 添加，扫到的版本立刻可启动。
- **原则**：
  1. 不污染游戏目录之外的东西（除 `versions\<name>\.vcl\` 这一个私有元数据目录）。
  2. 不复制任何 GPL 项目的实现代码，只按公开规范（Mojang version json、Fabric meta、Forge installer）自己实现。
  3. **启动与加载器安装不自研**：交给 LightyLauncherLib。我们只做"外壳 + 目录 + 元数据 + 界面"，不重写 classpath 求值和 Forge processors。
  4. UI 与业务分离：UI 只读状态、只发 Intent；业务层不依赖 UI，可以命令行跑通。

---

## 1. 技术栈与依赖

| 项 | 选型 |
|---|---|
| 标准 | C++17，MSVC x64 |
| 构建 | CMake ≥ 3.20，顶层工程 + `add_subdirectory` |
| UI | **VCUI / XGStudio-cpp-UI**，源码内置于 `third_party/VCUI`，目标 `VCUI::vcui` |
| 基础能力 | **cpploclib / XGStudio-cpp-Core**，源码内置于 `third_party/cpploclib`：XGLOG / XGFS / XGCFG / XGDL / XGZIP / XGCRYPTO / XGPROC / XGNET / XGTHREAD / XGSHELL / XGCRASH |
| 网络 | XGNET（libcurl，Schannel） |
| 启动 / 安装 | **LightyLauncherLib**（Rust，编成静态库 `.lib` + C ABI 头） |
| MSVC 运行时 | **`/MD`（动态 CRT）全工程统一** |
| 许可证 | AGPL-3.0（本仓库）；内置依赖为 Apache-2.0，兼容 |

### 1.1 依赖内置（vendored copy）策略

依赖以**源码副本**进仓库，不用 submodule、不用 `find_package`：

```
third_party/
├─ VCUI/          ← 复制自 XG-cyhilu/XGStudio-cpp-UI
└─ cpploclib/     ← 复制自 XG-cyhilu/XGStudio-cpp-Core
```

约束：

1. **保持上游目录结构**（`include/` `src/` `demo/` `CMakeLists.txt` 原样保留），只在顶层用 `CACHE FORCE` 关掉 demo/test，方便整目录覆盖同步。
2. 内置版本的上游 commit、许可、以及**每一处本地修改**都记在 `docs/THIRD_PARTY.md`。
3. 同步方式 = 把上游新版本目录整体复制覆盖，再按 `THIRD_PARTY.md` 的清单重打本地补丁；不做双向 cherry-pick。
4. 允许为产品需要直接改内置源码，但改完必须回写 `THIRD_PARTY.md`，且**优先把改动推回上游**——这两个库都是自有的，能上游化就不要留在补丁清单里。

顶层默认关掉的开关（写成 `CACHE BOOL "" FORCE`）：

| 变量 | 值 |
|---|---|
| `VCUI_BUILD_DEMO` | OFF |
| `CPPLOCLIB_BUILD_DEMOS` | OFF |
| `CPPLOCLIB_BUILD_TESTS` | OFF |

libcurl 仍由 cpploclib 的 `XGDL_CURL_DIR` / `find_package(CURL)` 提供；顶层自己写 `vcpkg.json`（子目录清单在 `add_subdirectory` 形态下不生效），至少包含 `curl`。

### 1.2 MSVC 运行时：统一 `/MD`

选 `/MD` 而不是 `/MT`，原因是 **LightyLauncherLib 以静态库形式参与链接**：

- `/MD` 时 C++ 与 Rust（默认 `dynamic CRT`）共用同一份 `ucrtbase` + `vcruntime140`，**跨语言边界传内存/句柄是安全的**；
- `/MT` 会给 C++ 和 Rust 各链一份静态 CRT，同一进程里出现两套 CRT 全局状态（`errno`、locale、`FILE*`），跨边界释放内存必炸。

代价与配套：

- 发布包必须带 `vcruntime140.dll` / `vcruntime140_1.dll`（或检测 VC++ 运行库是否已装）；
- 全工程禁止局部改成 `/MT`：VCUI 的 `VCUI_STATIC_RUNTIME` 保持 OFF，libcurl 保持默认（当前 `.deps` 里编的就是 `/MD`）；
- Rust 侧不要开 `crt-static`。

---

## 2. 目录与文件约定

### 2.1 仓库结构

```
Void-Craft-Launcher--VCL/
├─ CMakeLists.txt
├─ vcpkg.json
├─ third_party/
│  ├─ VCUI/
│  └─ cpploclib/
├─ src/
│  ├─ main.cpp
│  ├─ app/            App 生命周期、窗口、路由、AppState
│  ├─ ui/             页面与组件（只读 state，发 Intent）
│  ├─ backend/        IBackend 接口、MockBackend、lighty（FFI 封装）
│  └─ core/           不依赖 UI 的目录/元数据/扫描/日志
├─ docs/
│  ├─ ARCHITECTURE.md
│  └─ THIRD_PARTY.md
└─ tools/             Lighty 静态库的取用/校验脚本（后续）
```

### 2.2 Root（一个 Root = 一个标准 `.minecraft`）

```
<Root>
├── libraries\
├── assets\indexes\ , assets\objects\
├── versions\
├── launcher_profiles.json     ← Forge/NeoForge 安装器会校验它
└── PCL\ ...                   ← PCL 自己的东西，VCL 只读不写
```

Root 合法性判定：`versions\` 目录存在。**少于这个条件的文件夹不允许被添加。**

### 2.3 版本目录（一个版本 = 一个实例）

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

### 2.4 gameDir（版本隔离）

| 模式 | gameDir | 说明 |
|---|---|---|
| `VersionDir` | `<Root>\versions\<folder>\` | 版本隔离（PCL 默认隔离时的布局） |
| `RootDir` | `<Root>\` | 不隔离（mods 与所有版本共享） |
| `Custom` | 用户指定 | 高级需求 |

导入已有目录时的**启发式默认**：

- `versions\<folder>\` 下存在 `mods` / `config` / `saves` / `options.txt` 之一 → `VersionDir`
- 否则 → `RootDir`

UI 上必须能手动改，导入结果不做成"不可逆判断"。

### 2.5 全局配置与缓存（不写进游戏目录）

```
%APPDATA%\XGStudio\VoidCraftLauncher\global.json
%LOCALAPPDATA%\XGStudio\VoidCraftLauncher\
├── cache\natives\<rootHash>\<folder>\        ← natives 解压目标（若 Lighty 不接管）
├── cache\meta\                                ← version manifest / fabric profile 等缓存
├── cache\downloads\                           ← 临时下载
├── lighty\                                    ← Lighty 自己的状态（登录令牌等），由它管理
└── logs\<rootHash>\<folder>\launch-YYYYMMDD-HHMMSS.log
```

`rootHash` = Root 绝对路径（小写、正斜杠归一后）的 SHA-1 前 8 字节十六进制。

**natives 与日志的硬约束不变**：natives 不进版本目录（多实例并发不互相覆盖）；日志每实例一份。
**Lighty 若需要自有状态目录，只允许落在 `%LOCALAPPDATA%\XGStudio\VoidCraftLauncher\lighty\`**，见 §5.6。

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
  "ui": { "language": "zh-CN", "theme": "auto", "opacity": 0.9, "accent": "#0071EA" },
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

配置默认：每实例一份，**没有 Root 级"默认值继承"**（v0.1 不做）。
`instance.json` 缺失时按"全部默认 + 启发式 gameDir"即时生成，不阻断启动。

---

## 4. 模块划分与接口

```
src/
├── main.cpp
├── app/
│   ├── App.h/.cpp          窗口与生命周期（VCUI Surface/App）
│   ├── Router.h/.cpp       页面栈与切换
│   └── State.h/.cpp        AppState + Intent 分发（唯一可写状态的地方）
├── ui/
│   ├── shell/              标题栏、侧边导航、内容容器、状态栏
│   ├── pages/              各页面（清单待定，见 §8）
│   └── widgets/            复用组件（实例卡片、任务行、设置项）
├── backend/
│   ├── IBackend.h          四个能力的抽象接口
│   ├── MockBackend.cpp     界面阶段用的假数据实现
│   └── lighty/             LightyLauncherLib 的 FFI 封装（§5）
└── core/
    ├── root_manager
    ├── version_scanner
    ├── instance_store
    ├── meta_cache
    └── log_hub
```

命名空间统一 `vcl`；目录小写下划线；每个头文件 `#pragma once`。
路径内部统一 **UTF-8 `std::string`**，只在 Win32/Lighty 边界转宽字符或原样透传。

### 4.1 RootManager

```cpp
struct RootInfo { std::string path; std::string name; bool valid; };

class RootManager {
public:
    bool Load(const std::string& globalJsonPath);      // XGCFG
    bool Save();
    const std::vector<RootInfo>& Roots() const;
    bool AddRoot(const std::string& path, std::string& err);   // 校验 versions\ 存在
    bool RemoveRoot(size_t index);
    int  CurrentIndex() const;
    bool SetCurrent(size_t index);
};
```

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

只读 `versions\*\*.json` 的**头部字段**（`id` / `type` / `mainClass` / `inheritsFrom` / `libraries` 里的 loader 关键字），不做完整解析——
classpath 求值已经交给 Lighty。扫描必须容忍坏 JSON，坏文件跳过并记一条日志。

### 4.3 InstanceStore

`.vcl\instance.json` 的读写与默认值，按 §3.2。缺失即按默认生成（启发式 gameDir），写回时机是"用户改设置"或"启动结束"。

### 4.4 MetaCache

`cache\meta` 下的小 JSON 缓存（version manifest、根目录列表指纹等），key = URL 的 FNV-1a，带 TTL 与 ETag 式校验。
**只缓存我们自己要读的东西**，Lighty 的缓存由它自己管。

### 4.5 LogHub

XGLOG 的薄封装：每实例一个日志文件（§2.5），启动器自身日志一份；UI 日志面板通过 `UiPoll` 拉取。
JVM 输出由 Lighty 的事件流转发进来，与启动器日志同文件、不同级别。

### 4.6 IBackend

```cpp
struct IBackend {
    virtual ~IBackend() = default;

    virtual std::vector<VersionEntry> ListVersions(const RootInfo& root) = 0;
    virtual bool  Install(const InstallRequest& req, IProgressSink& sink, std::string& err) = 0;
    virtual bool  Launch(const LaunchRequest& req, ILaunchSink& sink, std::string& err) = 0;
    virtual std::vector<JavaInfo> FindJava() = 0;
};
```

- **界面阶段只接 `MockBackend`**：固定的 2~3 个实例、0.3s 一跳的假进度、假日志行。
- **真实阶段只接 `lighty::Backend`**：四个方法映射到 §5 的 C ABI，页面代码不动。
- `IProgressSink` / `ILaunchSink` 都是**轮询式**：`Poll()` 取事件，不回调。

### 4.7 不属于本项目的部分

以下能力**由 LightyLauncherLib 提供**，VCL 不实现、不复制：

- `inheritsFrom` 递归合并、`rules` 求值、classpath / natives 处理；
- JVM 参数与游戏参数拼装、变量替换；
- 启动器进程管理（起 JVM、收 stdout/stderr、退出码）；
- Fabric / Quilt / Forge / NeoForge 安装；
- 认证（离线 UUID 派生、微软登录、令牌刷新）。

我们保留的唯一理由是"UI 需要展示"：`VersionScanner` 的 loader 关键字探测、`JavaInfo` 列表、安装/启动事件流。

---

## 5. LightyLauncherLib 集成契约

### 5.1 交付形态

| 项 | 约定 |
|---|---|
| 上游 | `Lighty-Launcher/LightyLauncherLib`（MIT），本地克隆 `D:\XGstudio\LightyLauncherLib`，版本 `26.11.0` |
| 产物 | `lighty_capi.lib`（staticlib）+ `include/lighty_capi.h` |
| 放置 | `third_party/lighty/lib/x64/Release/lighty_capi.lib`、`third_party/lighty/include/lighty_capi.h` |
| 构建 | `crates/capi`（本项目新增的 C ABI 层）`cargo build --release -p lighty-capi`；见 §5.1.1 的环境前提 |
| 校验 | 二进制 SHA-256 记在 `docs/THIRD_PARTY.md` |
| 链接 | `/MD`（默认），另需系统库 `bcrypt ws2_32 crypt32 secur32 userenv advapi32 ole32 oleaut32 ntdll` |
| 特性 | `vanilla,fabric,quilt,forge,neoforge,events`（不含 modrinth/curseforge/keyring） |

#### 5.1.1 构建环境（实测）

- Rust `1.99.1` stable `x86_64-pc-windows-msvc`（workspace 要求 1.98+）
- `cmake` 必须在 PATH（`aws-lc-sys` 用它编 BoringSSL）：`D:\VisualStudio\2022\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`
- `AWS_LC_SYS_PREBUILT_NASM=1`（本机无 NASM，否则 build script 会尝试从 GitHub 下载 nasm 而卡死）
- 国内网络用镜像：`--config source.crates-io.replace-with="rsproxy-sparse" --config source.rsproxy-sparse.registry="sparse+https://rsproxy.cn/index/"`
- Release profile 是 `panic = "abort"` + `lto = "thin"` + `codegen-units = 1`，链接约 12 分钟；`panic=abort` 意味着 **Rust 侧一旦 panic 直接终止进程**，所以 C ABI 层不 `unwrap`、不 `expect`，错误一律转成返回码。

#### 5.1.2 体积实测（2026-11-24）

`.lib` 本身 79.7 MB 是**未链接归档**（windows-sys 26MB、tokio 11MB、rustls 9.8MB、aws-lc 9.5MB 全堆着不裁剪），不代表发布体积。

真实链接结果（`/OPT:REF /OPT:ICF`，`/O2 /MD`，probe 里把 `init` → `offline_uuid` → `job_launch` → `poll` → `cancel` 全调了一遍，即整条 launch 路径都被拉进来）：

| 项 | 体积 |
|---|---|
| Rust 部分（Lighty + tokio + reqwest + rustls/BoringSSL） | **6.21 MB** |
| 预算 | 10 MB 以内 |
| 余量 | ~3.8 MB，留给 VCUI/D2D/核心模块 |

所以不需要为体积换 crypto provider，也不需要砍 loader 特性。


### 5.2 C ABI（v0，已实现）

```c
const char *lighty_version(void);                                   // "26.11.0"
int32_t     lighty_init(const char *name);                          // 0 ok / 1 already / -1 err
int32_t     lighty_offline_uuid(const char *user, char *out, size_t cap);

LightyLaunchJob *lighty_job_launch(const char *request_json);       // NULL = 请求非法
int32_t     lighty_job_poll(LightyLaunchJob *job, char *buf, size_t cap); // 1 事件 / 0 空闲 / 2 成功 / 3 失败 / -2 缓冲小 / -1 句柄错
int32_t     lighty_job_error(LightyLaunchJob *job, char *buf, size_t cap);
int32_t     lighty_job_cancel(LightyLaunchJob *job);
void        lighty_job_free(LightyLaunchJob *job);
```

请求 JSON（`lighty_job_launch`）：

```json
{
  "instance": "1.21.1-vanilla", "loader": "vanilla", "loader_version": "",
  "mc_version": "1.21.1", "player": "Steve",
  "memory_mb": 4096, "width": 1280, "height": 720,
  "game_dir": "D:\\Games\\MC", "java": "temurin"
}
```

规则（已落地到 `crates/capi/src/lib.rs`）：

- 只用 C 类型：`const char*` / `int32_t` / 不透明句柄 `LightyLaunchJob*`；UTF-8。
- **不回调**：tokio 多线程 runtime 在 FFI 内部懒加载（4 worker），所有产出经轮询读取。
- 事件不自己造结构：直接把 `lighty_event::Event` 的 serde JSON 吐出来，C++ 侧按 `type` 字段分发。
- 所有字符串写进调用方缓冲区，缓冲不足返回 `-2` 且不写半截。
- `panic = "abort"` 下不可能 `catch_unwind`，所以 ABI 层不产生 panic（无 `unwrap`/`expect`/索引越界）。

### 5.3 轮询模型

```
C++                                     本进程 (Rust)
lighty_job_launch(json)  ─────────────▶ spawn(async: auth → VersionBuilder → launch)
lighty_job_poll(job, buf, cap) ◀──────  broadcast 事件队列（容量 8192）
lighty_job_cancel(job)                     abort handle
```

- UI 每 200ms 调一次 `lighty_job_poll` **循环**，直到返回 `0`/`2`/`3`。
- 队列溢出会吐一条 `{"type":"Lagged","data":{"skipped":N}}`，UI 据此忽略进度回跳。
- 完成态不额外造事件：`poll` 返回 `2`（成功）或 `3`（失败，再调 `lighty_job_error`）。

### 5.4 日志

Lighty 的 `tracing` 输出本次未启用（`tracing` feature 未开），日志来源只有事件流里的
`ConsoleOutput`（游戏 stdout/stderr）。启动器侧日志仍由 `core::LogHub` 记录我们自己的状态。
若后续需要 Lighty 内部日志，把 `tracing` feature 打开并在此登记一个 `tracing_subscriber` 落盘订阅器。


### 5.5 生命周期

一个 `Backend` 对应一个 Lighty 全局上下文；`lighty_init(...)` 在 App 启动时调一次，`lighty_shutdown()` 在退出时调。
安装/启动各自持有句柄，完成后 `lighty_*_destroy(handle)`。

### 5.6 目录约束（硬要求）

- Lighty 必须能接受**显式传入的 root / gameDir / nativesDir / javaPath**，不得自己拍脑袋推导路径。
- Lighty 允许有自己的状态（登录令牌、下载缓存），但只能落在 `%LOCALAPPDATA%\XGStudio\VoidCraftLauncher\lighty\`，**不得在 `<Root>` 下新建私有目录**（§0 原则 1）。
- 若它默认要写 `.lighty/`、`.minecraft/launcher_accounts.json` 之类，我们必须能关掉或改路径；不能改就由这层做映射。

### 5.7 接入前待验证清单

- [x] Lighty 提供「只装 + 只启动」的最小入口：`VersionBuilder(name, loader, loader_version, mc_version)` + `.launch(&profile, dist).run()`（安装是启动的前置步骤，幂等）
- [x] 自带 Java 探测与下载：`lighty-java`（Temurin/GraalVM/Zulu/Liberica），**删掉我们的 `JavaFinder`**
- [x] 离线 UUID 用 `OfflinePlayer:<name>` v5（`lighty_auth::generate_offline_uuid`）
- [ ] **是否会写 Root 下的私有文件**：见 §5.8，答案是不但要写，而且目录根本不在我们手里
- [x] 退出码与崩溃信息：经 `Event::InstanceExited` / `ConsoleOutput` 透出
- [x] 静态库 `/MD` 体积：79.7 MB（含 BoringSSL），与 cpploclib 的 curl 无符号冲突

### 5.8 已知缺口：实例目录不可指定（必须先解决）

Lighty 的磁盘布局是**它自己的**，不是 PCL 的：

```
%APPDATA%\<lighty_init 的 name>\<instance name>\
    ├── libraries\  assets\  natives\  versions\ ...
%APPDATA%\<name>\jre\        ← 实际在 %LOCALAPPDATA% 侧（config_dir）
```

`VersionBuilder::new` 里 `game_dirs = AppState::data_dir().join(name)` 是**私有字段、无公开 setter**
（只有 `runtime_dir` 能通过 `VersionInfo::set_runtime_dir` 改）。`Installer::create_directories`
用的正是 `game_dirs`。

后果：想"把用户已有的 `.minecraft` 当 Root"（本文 §0 兼容目标），当前做不到——Lighty 只会在
`%APPDATA%\XGStudio\VoidCraftLauncher\<instance>` 里造一个新实例。

三条出路（待决策，见下方"待定"）：

1. **给上游加 setter**（推荐）：`VersionBuilder::with_game_dirs(PathBuf)` + `#InstanceDir` 用法，
   由我们把小补丁提到 `Lighty-Launcher/LightyLauncherLib`（MIT 允许）。改动 10 行以内，`game_dirs`
   直接指向 `<Root>\versions\<folder>`，installer/runner 全部沿用。
2. 放弃"标准 `.minecraft` 兼容"，VCL 变成 Lighty 目录模型的壳（与 §0 原则冲突，不建议）。
3. 在我们这层复制目录/做 Windows 符号链接把两边拼起来（脆弱，不做）。


---

## 6. 启动流程（v0.2）

```
1. 载入 global.json → 选中 Root（core::RootManager）
2. 扫 versions → 版本列表（core::VersionScanner）
3. 选中实例 → 读 .vcl\instance.json（core::InstanceStore，缺失即默认）
4. 组装 LaunchRequest（root, folder, gameDir, memoryMb, jvmArgs, javaPath, 离线身份）
5. backend::Launch → lighty_launch_start(...)
6. UI 每 200ms: lighty_launch_poll → 状态/日志/进度 → LogHub + 日志面板
7. Finished：写回 lastPlayedAt / totalPlayTimeSec
8. 非 0 退出 + 日志含 Exception → 给"查看日志 / 打开目录"
```

安装（原版 / loader）走同一套：`InstallRequest → lighty_install_start → poll → Finished → 重扫 versions`。

---

## 7. 里程碑

| 版本 | 内容 |
|---|---|
| **v0.1** | **UI 骨架**：Shell（标题栏/导航/内容区）、路由、`AppState`、`MockBackend`；core 的 RootManager / VersionScanner / InstanceStore 可命令行跑通 |
| v0.2 | Lighty 接入：原版安装 + 启动 + 离线身份 + 每实例日志（替换 Mock） |
| v0.3 | loader 安装（Fabric/Quilt/Forge/NeoForge，经 Lighty） |
| v0.4 | 多 Root 管理、设置页、下载页真实化 |
| v0.5 | 微软登录 |
| v0.6 | 崩溃日志包、更新检查、Modrinth |

---

## 8. v0.1 任务清单

工程与依赖：

- [ ] 顶层 `CMakeLists.txt`：`add_subdirectory(third_party/VCUI)` + `add_subdirectory(third_party/cpploclib)`，FORCE 关掉 demo/test，全工程 `/MD`
- [ ] 顶层 `vcpkg.json`（`curl` 等）
- [ ] `docs/THIRD_PARTY.md`：VCUI / cpploclib 的上游 commit、许可、本地补丁清单

UI 骨架（**页面清单已定，见下**）：

- [ ] `app::App`：VCUI 窗口 + 自定义标题栏 + DPI
- [ ] `app::Router`：页面栈、切换动画钩子
- [ ] `app::State`：`AppState` + `Intent` 分发（Mock 数据）
- [ ] `ui::shell`：侧边导航（选中态/图标位）、内容容器、状态栏
- [ ] 导航顺序固定为：**主页 / 版本 / 下载 / 设置 / 管理 / 更多**
- [ ] 主题：浅/深切换、强调色、透明度设置项（值先只作用于背景层）

页面（全部基于 VCUI，不混用其它控件层）：

| 顺序 | 页面 | v0.1 | 内容 |
|---|---|---|---|
| 1 | **主页** | 先做 | 当前实例卡（图标/名称/loader/内存）、启动按钮、最近日志入口、快速跳转 |
| 2 | 版本 | 骨架 | 实例列表（Mock）、版本隔离标识、导入/删除入口（先空实现） |
| 3 | **下载** | 先做 | 版本清单列表（Mock 数据）、loader 筛选、安装任务队列（假进度） |
| 4 | **设置** | 先做 | 主题/语言/内存默认值/Java 路径/下载镜像/缓存目录（只落 `global.json`） |
| 5 | 管理 | 骨架 | 多 Root 列表、添加/移除 Root、缓存与大文件清理入口 |
| 6 | 更多 | 骨架 | 关于、更新检查占位、日志目录、崩溃包入口 |

> v0.1 只把「主页 / 下载 / 设置」做成能点、能切主题、能跑假任务的完整骨架；其余三页给空壳与导航项。


core（不依赖 UI）：

- [ ] `core::RootManager`（XGCFG 读写 `global.json`）
- [ ] `core::VersionScanner`（`versions\*\*.json` → `VersionEntry`，探 loader）
- [ ] `core::InstanceStore`（`.vcl\instance.json` 读写 + 默认 + 启发式 gameDir）
- [ ] `core::LogHub`（XGLOG 每实例文件）
- [ ] 自检 CLI：`vcl_core_check.exe <root>` 打印 Root 合法性 / 版本数 / 每实例 gameDir 判定

backend：

- [ ] `IBackend` + `MockBackend`（假实例、假进度、假日志）

---

## 9. 编码规范

- 源码 UTF-8 **带 BOM**（外部 MSVC 不加 `/utf-8` 时按 936 解析会炸）。
- 编译选项 `/utf-8 /W3`，`cxx_std_17`，运行时 `/MD`（全工程一致，见 §1.2）。
- 错误传递用 `bool + std::string& err`（不抛异常穿越模块边界；FFI 边界见 §5.2）。
- 所有对外文件路径参数用 UTF-8 `std::string`。
- 每个文件 AGPL-3.0 头 + SPDX；内置依赖保留其 Apache-2.0 头，不改。
- UI 层禁止直接调 `core`/`backend` 的写接口：只能通过 `Intent`。
- 新增依赖必须先进入 `docs/THIRD_PARTY.md`，否则 PR 不接收。
