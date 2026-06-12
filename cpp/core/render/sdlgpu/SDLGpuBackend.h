#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct SDL_GPUDevice;
struct SDL_GPUTexture;

namespace krkr::render::sdlgpu {

enum class PixelFormat {
    R8,
    RGBA8,
    BGRA8,
};

struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    bool renderTarget = false;
    const char *debugName = nullptr;
};

struct TextureStats {
    uint32_t textureCount = 0;
    uint64_t textureBytes = 0;
    uint64_t uploadBytes = 0;
};

using TextureHandle = uint64_t;

class Backend {
public:
    Backend();
    ~Backend();

    Backend(const Backend &) = delete;
    Backend &operator=(const Backend &) = delete;

    Backend(Backend &&) noexcept;
    Backend &operator=(Backend &&) noexcept;

    bool Initialize(const char *preferredDriver = nullptr, bool debugMode = false);
    void Shutdown();

    bool IsReady() const;
    const char *DriverName() const;
    const std::string &LastError() const;
    TextureStats Stats() const;
    SDL_GPUDevice *Device() const;

    TextureHandle CreateTexture2D(const TextureDesc &desc);
    bool UploadTexture2D(TextureHandle handle, const void *pixels,
                         uint32_t pitch, uint32_t x, uint32_t y,
                         uint32_t width, uint32_t height);
    void DestroyTexture(TextureHandle handle);
    SDL_GPUTexture *NativeTexture(TextureHandle handle) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace krkr::render::sdlgpu
