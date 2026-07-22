#include <cstdio>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// Desktop Cocos host was removed. Production runtime is Flutter + SDL3 on
// Android (SdlRuntimeActivity). Native desktop entry is intentionally a stub.
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    static auto core_logger = spdlog::stdout_color_mt("core");
    spdlog::set_default_logger(core_logger);
    spdlog::error(
        "krkr2 native desktop Cocos host has been removed. "
        "Use the Flutter Android SDL3 runtime (SdlRuntimeActivity).");
    std::fprintf(stderr,
                 "krkr2: Cocos desktop host removed; use Flutter Android.\n");
    return 1;
}
