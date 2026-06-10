#pragma once

#include <cstdint>

namespace TVPBgfx {

void SetNativeWindow(void *nativeWindow);
bool InitializeVulkan(uint32_t width, uint32_t height);
bool IsReady();
void Shutdown();

} // namespace TVPBgfx
