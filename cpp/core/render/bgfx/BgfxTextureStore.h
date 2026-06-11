#pragma once

#include <cstdint>

namespace TVPBgfx {

uint16_t CreateManagedTexture2D(uint32_t width, uint32_t height,
                                const void *pixel, int pitch, int format);
uint16_t CreateEmptyManagedTexture2D(uint32_t width, uint32_t height,
                                     int format);
void UpdateManagedTexture2DRect(uint16_t handle, uint32_t textureWidth,
                                uint32_t textureHeight, uint32_t x,
                                uint32_t y, uint32_t width, uint32_t height,
                                const void *pixel, int pitch, int format);
void DestroyManagedTexture2D(uint16_t handle);
uint64_t GetManagedTextureStoreBytes();
uint64_t GetManagedTextureStoreBudgetBytes();
void ResetManagedTextureStore();

} // namespace TVPBgfx
