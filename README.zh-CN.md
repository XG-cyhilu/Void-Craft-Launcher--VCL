# Void Craft Launcher（VCL）

用 C++17 + Direct2D 自绘实现的 Minecraft Java 版启动器。

> 状态：重构中。Electron 版本已移除，C++ 代码库正在从零搭建。

## 特点

- **目录结构与标准 `.minecraft` 完全一致**：PCL、官方启动器或手搓的游戏文件夹都能直接添加，扫到的版本立刻可启动。
- 版本隔离 + 每实例独立配置，元数据放在 `versions\<版本名>\.vcl\`，不干扰其他启动器。
- 加载器：Fabric / Quilt（原生 JSON 安装），Forge / NeoForge（调用官方安装器）。
- 原生 Win32 + Direct2D，不依赖任何 Web 运行时。

## 技术栈

| 层 | 选型 |
|---|---|
| 语言 | C++17（MSVC x64） |
| 构建 | CMake |
| UI | VCUI——自绘 Direct2D 控件库 |
| 基础能力 | `cpploclib` 的 `xglog` / `xgfs` / `xgcfg`  / `xgzip` / `xgcrypto` / `xgproc` / `xgnet` / `xgthread` |

## 设计文档

目录约定、数据格式、模块接口、v0.1 任务清单见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 许可证

AGPL-3.0，见 [LICENSE](LICENSE)。
