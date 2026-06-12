#pragma once

#include "SDLGpuBackend.h"
#include "RenderManager.h"

#include <cstdint>
#include <string>

namespace krkr::render::sdlgpu::tvp {

struct UploadResult {
    bool uploaded = false;
    bool converted = false;
    uint64_t uploadBytes = 0;
    std::string error;
};

bool IsSupportedSourceFormat(TVPTextureFormat::e format);
uint32_t SourceBytesPerPixel(TVPTextureFormat::e format);
PixelFormat ToUploadPixelFormat(TVPTextureFormat::e format);

bool MakeTextureDesc(iTVPTexture2D &source, TextureDesc &desc,
                     std::string &error, const char *debugName = nullptr);
TextureHandle CreateTexture2D(Backend &backend, iTVPTexture2D &source,
                              const char *debugName = nullptr,
                              std::string *error = nullptr);
UploadResult UploadTexture2D(Backend &backend, TextureHandle handle,
                             iTVPTexture2D &source,
                             const tTVPRect *sourceRect = nullptr);

} // namespace krkr::render::sdlgpu::tvp
