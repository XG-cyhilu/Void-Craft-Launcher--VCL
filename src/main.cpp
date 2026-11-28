#include <windows.h>
#include "app/App.h"

namespace {

using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
using SetAwarenessFn = HRESULT(WINAPI*)(int);

// 进程级 DPI 感知：不做的话 Windows 会把整个窗口按系统缩放位图拉伸，
// 界面在 150% 下全部模糊，1px 描边会被拉成 2~3px 的灰带。
// 优先 PerMonitorV2，老系统逐级退回。
void EnableDpiAwareness() {
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        auto setCtx = reinterpret_cast<SetCtxFn>(
            reinterpret_cast<void*>(GetProcAddress(u, "SetProcessDpiAwarenessContext")));
        // (DPI_AWARENESS_CONTEXT)-4 = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        if (setCtx && setCtx((DPI_AWARENESS_CONTEXT)-4)) return;
    }
    if (HMODULE s = LoadLibraryW(L"shcore.dll")) {
        auto setAware = reinterpret_cast<SetAwarenessFn>(
            reinterpret_cast<void*>(GetProcAddress(s, "SetProcessDpiAwareness")));
        if (setAware && SUCCEEDED(setAware(2))) return;   // PROCESS_PER_MONITOR_DPI_AWARE
    }
    SetProcessDPIAware();
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    EnableDpiAwareness();
    vcl::App app;
    return app.Run();
}
