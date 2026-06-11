#pragma once

#include <cstdint>

namespace TVPBgfx {

void SetNativeWindow(void *nativeWindow);
void SetBackbufferSize(uint32_t width, uint32_t height);
bool InitializeVulkan(uint32_t width, uint32_t height);
bool IsReady();
uint16_t CreateTexture2D(uint32_t width, uint32_t height, const void *pixel,
                         int pitch, int format);
void UpdateTexture2D(uint16_t handle, uint32_t width, uint32_t height,
                     const void *pixel, int pitch, int format);
void DestroyTexture2D(uint16_t handle);
void UploadSoftwareFrame(uint32_t width, uint32_t height, const void *pixel,
                         int pitch, int format);
void StageRectBatch(int targetLeft, int targetTop, int targetWidth,
                    int targetHeight, uint32_t textureCount);
void StageTriangleBatch(uint32_t nTriangles, int clipLeft, int clipTop,
                        int clipWidth, int clipHeight,
                        const double *targetPointsXY);
void Shutdown();

} // namespace TVPBgfx
