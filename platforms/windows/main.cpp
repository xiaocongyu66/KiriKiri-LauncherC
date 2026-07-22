#include "main.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// Desktop Cocos host was removed. Production runtime is Flutter + SDL3 on
// Android (SdlRuntimeActivity). Native desktop entry is intentionally a stub.
int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPTSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    spdlog::set_level(spdlog::level::debug);
    static auto core_logger = spdlog::stdout_color_mt("core");
    spdlog::set_default_logger(core_logger);
    spdlog::error(
        "krkr2 native desktop Cocos host has been removed. "
        "Use the Flutter Android SDL3 runtime (SdlRuntimeActivity).");
    MessageBoxW(nullptr,
                L"Cocos desktop host removed.\nUse Flutter Android SDL3 runtime.",
                L"krkr2", MB_OK | MB_ICONINFORMATION);
    return 1;
}
