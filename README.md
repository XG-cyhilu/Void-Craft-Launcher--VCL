# Void Craft Launcher (VCL)

A Minecraft: Java Edition launcher written in C++17 with a Direct2D UI.

> Status: refactor in progress. The Electron implementation has been removed; the C++ codebase is being rebuilt from scratch.

## Highlights

- **Drop-in compatible with `.minecraft` layout** — add any standard game folder (created by PCL, the official launcher, or manually) and its versions show up immediately.
- Version isolation with per-instance settings, stored in `versions\<name>\.vcl\`.
- Loader support: Fabric / Quilt (native JSON), Forge / NeoForge (through their official installers).
- Native Win32 + Direct2D rendering, no web runtime.

## Tech stack

| Layer | Choice |
|---|---|
| Language | C++17 (MSVC x64) |
| Build | CMake |
| UI | VCUI — self-drawn Direct2D toolkit |
| Core utilities | `xglog`, `xgfs`, `xgcfg`, `xgdl`, `xgzip`, `xgcrypto`, `xgproc`, `xgnet`, `xgthread` from `cpploclib` |

## Design

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the directory conventions, data schemas, module interfaces and the v0.1 task list.

## License

AGPL-3.0. See [LICENSE](LICENSE).
