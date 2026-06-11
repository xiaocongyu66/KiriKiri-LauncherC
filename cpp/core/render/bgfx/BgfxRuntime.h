#pragma once

#include <cstdint>

namespace TVPBgfx {

void SetNativeWindow(void *nativeWindow);
void SetBackbufferSize(uint32_t width, uint32_t height);
bool InitializeVulkan(uint32_t width, uint32_t height);
bool IsReady();
uint16_t CreateTexture2D(uint32_t width, uint32_t height, const void *pixel,
                         int pitch, int format);
uint16_t CreateEmptyTexture2D(uint32_t width, uint32_t height, int format);
void UpdateTexture2D(uint16_t handle, uint32_t width, uint32_t height,
                     const void *pixel, int pitch, int format);
void UpdateTexture2DRect(uint16_t handle, uint32_t textureWidth,
                         uint32_t textureHeight, uint32_t x, uint32_t y,
                         uint32_t width, uint32_t height, const void *pixel,
                         int pitch, int format);
void DestroyTexture2D(uint16_t handle);
uint64_t GetManagedTextureBytes();
uint64_t GetManagedTextureBudgetBytes();
void UploadSoftwareFrame(uint32_t width, uint32_t height, const void *pixel,
                         int pitch, int format);
struct RectBatchCommand {
    const char *methodName = nullptr;
    int targetLeft = 0;
    int targetTop = 0;
    int targetWidth = 0;
    int targetHeight = 0;
    uint32_t textureCount = 0;
};
struct TriangleBatchCommand {
    const char *methodName = nullptr;
    uint32_t triangleCount = 0;
    int clipLeft = 0;
    int clipTop = 0;
    int clipWidth = 0;
    int clipHeight = 0;
    const double *targetPointsXY = nullptr;
};
void SubmitRectBatch(const RectBatchCommand &command);
void SubmitTriangleBatch(const TriangleBatchCommand &command);
void StageRectBatch(const char *methodName, int targetLeft, int targetTop,
                    int targetWidth, int targetHeight, uint32_t textureCount);
void StageTriangleBatch(const char *methodName, uint32_t nTriangles,
                        int clipLeft, int clipTop, int clipWidth,
                        int clipHeight, const double *targetPointsXY);
void Shutdown();

} // namespace TVPBgfx
