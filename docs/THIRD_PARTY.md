# 第三方组件登记（THIRD_PARTY）

本仓库以**源码副本**方式内置依赖（见 `docs/ARCHITECTURE.md` §1.1）。所有内置依赖、本地修改与
待接入的外部库都必须在此登记；未登记的依赖不接收。

## 内置源码（vendored）

| 组件 | 内置位置 | 上游仓库 | 内置版本 | 许可 | 本地修改 |
|---|---|---|---|---|---|
| XGStudio-cpp-UI（VCUI） | `third_party/VCUI/` | `https://github.com/XG-cyhilu/XGStudio-cpp-UI.git` | `75a1a03`（docs: rewrite the performance section…） | Apache-2.0 | 待登记 |
| XGStudio-cpp-Core（cpploclib） | `third_party/cpploclib/` | `https://github.com/XG-cyhilu/XGStudio-cpp-Core.git` | `9b66d39`（docs(xgdl): full API reference…） | Apache-2.0 | 待登记 |

> 复制时剔除：`.git/`、`.build/`、`.build-dll/`、`modDownload/.deps/`、各模块 `demo/` 可保留（编译开关关掉）。

### 本地修改登记格式

每条一行，写明「文件 / 改了什么 / 为什么 / 是否已上游化」：

```
- third_party/VCUI/src/Surface.cpp：把 WM_NCHITTEST 的拖拽区域从 32px 改成主题度量 / 适配 VCL 标题栏 / 未上游化
```

**优先上游化**：两个库都是自有项目，能推回上游的改动就不要长期留在补丁清单里。

### 同步流程

1. 从上游拉新版本，记录新 commit；
2. 用新目录覆盖 `third_party/<组件>/`（保留 `.gitignore` 与本文件条目）；
3. 按本文件的补丁清单重打本地修改；
4. 在下方「同步记录」追加一行。

## 待接入（外部二进制）

| 组件 | 形态 | 版本 | 放置 | 许可 | 状态 |
|---|---|---|---|---|---|
| LightyLauncherLib | Rust `staticlib` → `lighty.lib` + `lighty.h` | **待定**（固定 tag/rev） | `third_party/lighty/{include,lib}` | 待确认 | 未接入，见 `ARCHITECTURE.md` §5 |

接入要求：

- 固定版本 + 预编译产物附 SHA-256，记录在此；
- 运行时 `/MD`、Rust 侧 `crt-static` 关闭；
- 先跑通 `ARCHITECTURE.md` §5.7 的待验证清单再改 `IBackend`。

## 系统库（不内置，靠工具链）

`d2d1 dwrite dwmapi uxtheme user32 gdi32 ole32 imm32`（VCUI 声明）、
`bcrypt ws2_32 crypt32 secur32 wldap32`（libcurl，cpploclib 声明）——均为 Windows SDK 自带。

libcurl 的取得方式：cpploclib 的 `XGDL_CURL_DIR`（私有 `.deps/curl-install`）或 `find_package(CURL)`（vcpkg 前缀），
必须与服务端 `/MD` 运行时一致。

## 同步记录

| 日期 | 组件 | 变更 |
|---|---|---|
| 2026-11-24 | VCUI | 首次内置 `75a1a03` |
| 2026-11-24 | cpploclib | 首次内置 `9b66d39` |
