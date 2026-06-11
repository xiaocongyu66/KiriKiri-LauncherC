#include "BgfxTextureStore.h"

#include "DebugIntf.h"
#include "../../visual/GraphicsLoaderIntf.h"

#if defined(KIRIKIRI_HAS_BGFX)
#include <bgfx/bgfx.h>
#endif

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>

namespace TVPBgfx {
namespace {

uint32_t TextureUploadCount = 0;
uint32_t TextureUploadSkipCount = 0;
bool LoggedIntermediateTextureUploadDisabled = false;
bool LoggedTextureBudget = false;
uint64_t TextureUploadBytes = 0;
std::unordered_map<uint16_t, uint32_t> TextureUploadSizes;

constexpr uint32_t MaxManagedTextureBytes = 10 * 1024 * 1024;
constexpr uint64_t MaxManagedTextureTotalBytes = 256 * 1024 * 1024;
constexpr uint32_t ParallelTextureConvertPixels = 512 * 1024;

#if defined(KIRIKIRI_HAS_BGFX)
constexpr uint16_t InvalidTextureHandle = bgfx::kInvalidHandle;
#else
constexpr uint16_t InvalidTextureHandle = UINT16_MAX;
#endif

bool CopyAsRgba8(std::vector<uint8_t> &out, uint32_t width, uint32_t height,
                 const void *pixel, int pitch, int format) {
    if(!pixel || !width || !height || pitch <= 0)
        return false;

    const auto *source = static_cast<const uint8_t *>(pixel);
    const size_t pixelCount = static_cast<size_t>(width) * height;
    out.resize(pixelCount * 4);

    auto forEachRow = [&](const auto &convertRow) {
        if(pixelCount >= ParallelTextureConvertPixels) {
            oneapi::tbb::parallel_for(
                oneapi::tbb::blocked_range<uint32_t>(0, height),
                [&](const oneapi::tbb::blocked_range<uint32_t> &range) {
                    for(uint32_t row = range.begin(); row != range.end(); ++row)
                        convertRow(row);
                });
        } else {
            for(uint32_t row = 0; row < height; ++row)
                convertRow(row);
        }
    };

    switch(format) {
    case 4:
        forEachRow([&](uint32_t row) {
            std::memcpy(out.data() + static_cast<size_t>(row) * width * 4,
                        source + static_cast<size_t>(row) * pitch,
                        static_cast<size_t>(width) * 4);
        });
        return true;
    case 3:
        forEachRow([&](uint32_t row) {
            const uint8_t *line = source + static_cast<size_t>(row) * pitch;
            uint8_t *dest = out.data() + static_cast<size_t>(row) * width * 4;
            for(uint32_t column = 0; column < width; ++column) {
                dest[column * 4 + 0] = line[column * 3 + 0];
                dest[column * 4 + 1] = line[column * 3 + 1];
                dest[column * 4 + 2] = line[column * 3 + 2];
                dest[column * 4 + 3] = 0xff;
            }
        });
        return true;
    case 1:
        forEachRow([&](uint32_t row) {
            const uint8_t *line = source + static_cast<size_t>(row) * pitch;
            uint8_t *dest = out.data() + static_cast<size_t>(row) * width * 4;
            for(uint32_t column = 0; column < width; ++column) {
                const uint8_t gray = line[column];
                dest[column * 4 + 0] = gray;
                dest[column * 4 + 1] = gray;
                dest[column * 4 + 2] = gray;
                dest[column * 4 + 3] = 0xff;
            }
        });
        return true;
    default:
        return false;
    }
}

bool SupportsManagedTextureFormat(int format) {
    return format == 1 || format == 3 || format == 4;
}

uint64_t GetManagedTextureTotalBudgetBytes() {
    uint64_t oldStyleBudget = TVPGetGraphicCacheLimit();
    if(!oldStyleBudget)
        oldStyleBudget = TVPGraphicCacheSystemLimit;
    if(!oldStyleBudget)
        oldStyleBudget = MaxManagedTextureTotalBytes;
    return std::min<uint64_t>(oldStyleBudget, MaxManagedTextureTotalBytes);
}

bool ShouldStageTextureUpload(uint32_t width, uint32_t height) {
    if(!width || !height)
        return false;

    const uint64_t totalBudgetBytes = GetManagedTextureTotalBudgetBytes();

    if(!LoggedTextureBudget) {
        LoggedTextureBudget = true;
        TVPAddLog(TJS_W("[renderer] bgfx managed texture budget single=") +
                  ttstr(static_cast<int>(MaxManagedTextureBytes /
                                         (1024 * 1024))) +
                  TJS_W("MB total=") +
                  ttstr(static_cast<int>(totalBudgetBytes / (1024 * 1024))) +
                  TJS_W("MB oldStyleLimit=") +
                  ttstr(static_cast<int>(TVPGetGraphicCacheLimit() /
                                         (1024 * 1024))) +
                  TJS_W("MB"));
    }

    const uint64_t bytes = static_cast<uint64_t>(width) * height * 4;
    if(bytes <= MaxManagedTextureBytes &&
       TextureUploadBytes + bytes <= totalBudgetBytes)
        return true;

    ++TextureUploadSkipCount;
    if(!LoggedIntermediateTextureUploadDisabled) {
        LoggedIntermediateTextureUploadDisabled = true;
        TVPAddLog(TJS_W("[renderer] bgfx intermediate texture upload budget active; large or over-budget software textures stay CPU-side."));
    }
    if(TextureUploadSkipCount <= 8 || TextureUploadSkipCount == 16 ||
       TextureUploadSkipCount == 32 || (TextureUploadSkipCount % 512) == 0) {
        TVPAddLog(TJS_W("[renderer] bgfx intermediate texture upload skipped #") +
                  ttstr(static_cast<int>(TextureUploadSkipCount)) + TJS_W(" ") +
                  ttstr(static_cast<int>(width)) + TJS_W("x") +
                  ttstr(static_cast<int>(height)));
    }
    return false;
}

#if defined(KIRIKIRI_HAS_BGFX)
void TrackTextureUpload(bgfx::TextureHandle handle, uint32_t width,
                        uint32_t height) {
    const uint32_t bytes = width * height * 4;
    TextureUploadSizes[handle.idx] = bytes;
    TextureUploadBytes += bytes;
}

void UntrackTextureUpload(uint16_t handle) {
    auto it = TextureUploadSizes.find(handle);
    if(it == TextureUploadSizes.end())
        return;
    if(TextureUploadBytes >= it->second)
        TextureUploadBytes -= it->second;
    else
        TextureUploadBytes = 0;
    TextureUploadSizes.erase(it);
}
#endif

} // namespace

uint16_t CreateManagedTexture2D(uint32_t width, uint32_t height,
                                const void *pixel, int pitch, int format) {
#if defined(KIRIKIRI_HAS_BGFX)
    if(!SupportsManagedTextureFormat(format))
        return InvalidTextureHandle;
    if(!ShouldStageTextureUpload(width, height))
        return InvalidTextureHandle;
    std::vector<uint8_t> rgba;
    if(!CopyAsRgba8(rgba, width, height, pixel, pitch, format))
        return InvalidTextureHandle;
    const bgfx::Memory *memory = bgfx::copy(rgba.data(),
                                           static_cast<uint32_t>(rgba.size()));
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        static_cast<uint16_t>(std::min<uint32_t>(width, UINT16_MAX)),
        static_cast<uint16_t>(std::min<uint32_t>(height, UINT16_MAX)), false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, memory);
    if(bgfx::isValid(handle)) {
        TrackTextureUpload(handle, width, height);
        ++TextureUploadCount;
        if(TextureUploadCount <= 8 || (TextureUploadCount % 256) == 0) {
            TVPAddLog(TJS_W("[renderer] bgfx texture upload #") +
                      ttstr(static_cast<int>(TextureUploadCount)) + TJS_W(" ") +
                      ttstr(static_cast<int>(width)) + TJS_W("x") +
                      ttstr(static_cast<int>(height)));
        }
        return handle.idx;
    }
#else
    (void)width;
    (void)height;
    (void)pixel;
    (void)pitch;
    (void)format;
#endif
    return InvalidTextureHandle;
}

uint16_t CreateEmptyManagedTexture2D(uint32_t width, uint32_t height,
                                     int format) {
#if defined(KIRIKIRI_HAS_BGFX)
    if(!SupportsManagedTextureFormat(format))
        return InvalidTextureHandle;
    if(!ShouldStageTextureUpload(width, height))
        return InvalidTextureHandle;
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        static_cast<uint16_t>(std::min<uint32_t>(width, UINT16_MAX)),
        static_cast<uint16_t>(std::min<uint32_t>(height, UINT16_MAX)), false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, nullptr);
    if(bgfx::isValid(handle)) {
        TrackTextureUpload(handle, width, height);
        ++TextureUploadCount;
        if(TextureUploadCount <= 8 || (TextureUploadCount % 256) == 0) {
            TVPAddLog(TJS_W("[renderer] bgfx texture allocate #") +
                      ttstr(static_cast<int>(TextureUploadCount)) + TJS_W(" ") +
                      ttstr(static_cast<int>(width)) + TJS_W("x") +
                      ttstr(static_cast<int>(height)));
        }
        return handle.idx;
    }
#else
    (void)width;
    (void)height;
    (void)format;
#endif
    return InvalidTextureHandle;
}

void UpdateManagedTexture2DRect(uint16_t handle, uint32_t textureWidth,
                                uint32_t textureHeight, uint32_t x,
                                uint32_t y, uint32_t width, uint32_t height,
                                const void *pixel, int pitch, int format) {
#if defined(KIRIKIRI_HAS_BGFX)
    bgfx::TextureHandle texture{handle};
    if(!bgfx::isValid(texture) || !pixel || !width || !height || pitch <= 0)
        return;
    if(!SupportsManagedTextureFormat(format))
        return;
    if(x >= textureWidth || y >= textureHeight)
        return;
    width = std::min(width, textureWidth - x);
    height = std::min(height, textureHeight - y);
    std::vector<uint8_t> rgba;
    if(!CopyAsRgba8(rgba, width, height, pixel, pitch, format))
        return;
    const bgfx::Memory *memory = bgfx::copy(
        rgba.data(), static_cast<uint32_t>(rgba.size()));
    bgfx::updateTexture2D(
        texture, 0, 0, static_cast<uint16_t>(std::min<uint32_t>(x, UINT16_MAX)),
        static_cast<uint16_t>(std::min<uint32_t>(y, UINT16_MAX)),
        static_cast<uint16_t>(std::min<uint32_t>(width, UINT16_MAX)),
        static_cast<uint16_t>(std::min<uint32_t>(height, UINT16_MAX)), memory,
        static_cast<uint16_t>(width * 4));
#else
    (void)handle;
    (void)textureWidth;
    (void)textureHeight;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)pixel;
    (void)pitch;
    (void)format;
#endif
}

void DestroyManagedTexture2D(uint16_t handle) {
#if defined(KIRIKIRI_HAS_BGFX)
    bgfx::TextureHandle texture{handle};
    if(bgfx::isValid(texture)) {
        bgfx::destroy(texture);
        UntrackTextureUpload(handle);
    }
#else
    (void)handle;
#endif
}

uint64_t GetManagedTextureStoreBytes() {
    return TextureUploadBytes;
}

uint64_t GetManagedTextureStoreBudgetBytes() {
    return GetManagedTextureTotalBudgetBytes();
}

void ResetManagedTextureStore() {
    TextureUploadBytes = 0;
    TextureUploadSizes.clear();
    TextureUploadCount = 0;
    TextureUploadSkipCount = 0;
    LoggedIntermediateTextureUploadDisabled = false;
    LoggedTextureBudget = false;
}

} // namespace TVPBgfx
