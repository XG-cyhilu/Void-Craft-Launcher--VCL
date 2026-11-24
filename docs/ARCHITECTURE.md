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
| 产物 | Rust `staticlib` → `lighty.lib` + `lighty.h`（cbindgen 生成） |
| 版本 | 固定 git rev / tag，记录在 `docs/THIRD_PARTY.md`；预编译产物带 SHA-256 |
| 构建 | 在 CI（`windows-latest` + `rustup`）产出，或本地 `cargo build --release`；不在业务工程里跑 cargo |
| 放置 | `third_party/lighty/{include,lib}`，`x64` Release/Debug 两套 |
| 链接 | `/MD`（默认，见 §1.2），`crt-static` **关** |

### 5.2 ABI 规则

- 全部 `extern "C"`，参数只用 C 类型（`const char*` UTF-8、`int32_t`、`uint64_t`、不透明句柄指针）。
- **C++ 类型不得出现在头文件里**：没有 `std::string`、没有引用、没有枚举 class。
- Rust 侧每个入口包 `catch_unwind`，panic 一律转成错误码，绝不跨边界。
- 错误用「返回码 + `last_error(handle)` 取字符串」；字符串生命周期由 Lighty 持有，调用方只读。
- 内存所有权：**谁分配谁释放**；跨边界只传「调用方提供的缓冲区」或「Lighty 分配 + Lighty 的 `free` 函数」。

### 5.3 线程与事件模型

与 XGDL/XGLOG 同一套口径——**不回调，轮询**：

```
C++                                    Lighty (Rust)
lighty_install_start(...)  ──────────▶  后台线程
lighty_install_poll(handle, &ev) ◀────  事件队列（容量固定，满了丢并计数）
lighty_install_cancel(handle)
```

事件种类至少：`Progress{done,total,bytesPerSec}`、`Log{level,text}`、`StateChanged`、`Finished{code}`、`Failed{code,msg}`。
进度用原子计数，UI 按 200ms 拉一次，与 `Snapshot()` 一致。

### 5.4 日志

Lighty 的 `tracing` 输出走一个注册的 C 回调（`void(*)(int level, const char* utf8)`），由 `lighty::Backend` 转进 `LogHub`。
回调里禁止重入 Lighty API。

### 5.5 生命周期

一个 `Backend` 对应一个 Lighty 全局上下文；`lighty_init(...)` 在 App 启动时调一次，`lighty_shutdown()` 在退出时调。
安装/启动各自持有句柄，完成后 `lighty_*_destroy(handle)`。

### 5.6 目录约束（硬要求）

- Lighty 必须能接受**显式传入的 root / gameDir / nativesDir / javaPath**，不得自己拍脑袋推导路径。
- Lighty 允许有自己的状态（登录令牌、下载缓存），但只能落在 `%LOCALAPPDATA%\XGStudio\VoidCraftLauncher\lighty\`，**不得在 `<Root>` 下新建私有目录**（§0 原则 1）。
- 若它默认要写 `.lighty/`、`.minecraft/launcher_accounts.json` 之类，我们必须能关掉或改路径；不能改就由这层做映射。

### 5.7 接入前待验证清单

- [ ] Lighty 是否提供「只装原版 + 只启动」的最小入口，而不强绑它自己的 UI/账户体系
- [ ] 是否自带 Java 探测（有则删掉我们的 `JavaFinder`，无则自留）
- [ ] 离线 UUID 是否按 `OfflinePlayer:<name>` 派生（必须与 Java 端一致，否则皮肤/数据对不上）
- [ ] 是否会写 Root 下除 `versions/`、`libraries/`、`assets/` 之外的文件
- [ ] 退出码与崩溃信息是否原样透出（我们要据此给"查看日志/打开目录"）
- [ ] 静态库在 `/MD` 下的体积与符号冲突情况（尤其和 cpploclib 的 curl 同时链）

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

UI 骨架（**页面清单未定，先做容器**）：

- [ ] `app::App`：VCUI 窗口 + 自定义标题栏 + DPI
- [ ] `app::Router`：页面栈、切换动画钩子
- [ ] `app::State`：`AppState` + `Intent` 分发（Mock 数据）
- [ ] `ui::shell`：侧边导航（选中态/图标位）、内容容器、状态栏
- [ ] 页面占位：至少一个可切换的空白页 + 一个"关于/占位"页，用于验证路由与布局
- [ ] 主题：浅/深切换、强调色、透明度设置项（值先只作用于背景层）

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
