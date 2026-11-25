cmake_minimum_required(VERSION 3.20)
project(VCL VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if (MSVC)
    add_compile_options(/utf-8 /W3)
endif()

# ---------------- 内置依赖 ----------------
# VCUI：只出静态库，不建 demo
set(VCUI_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(VCUI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(VCUI_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/VCUI EXCLUDE_FROM_ALL)

# cpploclib：只开需要的模块；curl 用内置的静态安装前缀
set(CPPLOCLIB_BUILD_DOWNLOAD ON CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_NET ON CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_CFG ON CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_FS ON CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_LOG ON CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_THREAD ON CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_CRYPTO ON CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_PROC OFF CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_ZIP OFF CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_SECURE OFF CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_SHELL OFF CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_CRASH OFF CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_CORE OFF CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CPPLOCLIB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpploclib EXCLUDE_FROM_ALL)

# libwebp：只留静态解码库（图标是 webp/png 混杂，webp 需要专门解码器）
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBP_JS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_FUZZTEST OFF CACHE BOOL "" FORCE)
set(WEBP_USE_THREAD OFF CACHE BOOL "" FORCE)
set(WEBP_ENABLE_SIMD ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/libwebp EXCLUDE_FROM_ALL)

# md4c：Mod 详情页的 Markdown 解析（C 库，两个 .c 就够，不带 md2html）
add_library(md4c STATIC
    third_party/md4c/md4c.c
    third_party/md4c/entity.c)
target_include_directories(md4c PUBLIC third_party/md4c)
target_compile_definitions(md4c PRIVATE _CRT_SECURE_NO_WARNINGS)

# ---------------- 应用 ----------------
set(VCL_SOURCES
    src/main.cpp
    src/app/App.cpp
    src/core/Text.cpp
    src/core/Paths.cpp
    src/core/Settings.cpp
    src/core/AsyncHttp.cpp
    src/core/Downloader.cpp
    src/core/McApi.cpp
    src/core/ModrinthApi.cpp
    src/core/Image.cpp
    src/core/IconStore.cpp
    src/core/ModWiki.cpp
    src/core/Markdown.cpp
    src/ui/Common.cpp
    src/ui/DownloadPage.cpp
    src/ui/IconImage.cpp
    src/ui/MarkdownView.cpp
    src/ui/SettingsPage.cpp
)

add_executable(vcl WIN32 ${VCL_SOURCES})
target_include_directories(vcl PRIVATE src)
target_compile_definitions(vcl PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(vcl PRIVATE
    VCUI::vcui
    XGDL::xgdl
    XGNET::xgnet
    XGCFG::xgcfg
    XGFS::xgfs
    XGLOG::xglog
    XGTHREAD::xgthread
    XGCRYPTO::xgcrypto
    webp
    md4c
    d2d1 dwrite dwmapi user32 gdi32 shell32 ole32 oleaut32 imm32
    uxtheme comdlg32 shlwapi advapi32 windowscodecs)

# resource（方块图标、ModWiki 汉化表）跟 exe 放一起，运行时不依赖源码树
add_custom_command(TARGET vcl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resource
            $<TARGET_FILE_DIR:vcl>/resource
    COMMENT "copy resource/ next to vcl.exe")

# 发布目录里 VCUI 需要 /MD 运行时 DLL；先放在构建目录，方便直接跑
if (MSVC)
    set(CMAKE_INSTALL_UCRT_LIBS ON)
endif()



