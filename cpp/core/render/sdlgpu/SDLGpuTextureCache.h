#pragma once

#include "SDLGpuTvpAdapter.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace krkr::render::sdlgpu::tvp {

struct TextureCacheLimits {
    uint64_t maxSingleTextureBytes = 10ull * 1024ull * 1024ull;
    uint64_t maxTotalTextureBytes = 256ull * 1024ull * 1024ull;
};

struct TextureCacheStats {
    uint32_t textureCount = 0;
    uint64_t textureBytes = 0;
    uint64_t uploadBytes = 0;
    uint64_t uploads = 0;
    uint64_t evictions = 0;
};

struct TextureCacheResult {
    TextureHandle handle = 0;
    bool created = false;
    bool uploaded = false;
    bool converted = false;
    uint64_t textureBytes = 0;
    uint64_t uploadBytes = 0;
    uint64_t evicted = 0;
    std::string error;
};

class TextureCache {
public:
    TextureCache() = default;
    explicit TextureCache(Backend *backend);
    ~TextureCache();

    TextureCache(const TextureCache &) = delete;
    TextureCache &operator=(const TextureCache &) = delete;

    void SetBackend(Backend *backend);
    void SetLimits(const TextureCacheLimits &limits);
    const TextureCacheLimits &Limits() const;
    TextureCacheStats Stats() const;

    TextureCacheResult Upsert(iTVPTexture2D &source,
                              const tTVPRect *sourceRect = nullptr,
                              const char *debugName = nullptr);
    void Release(const iTVPTexture2D *source);
    void Clear();

private:
    struct Record {
        TextureHandle handle = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        PixelFormat format = PixelFormat::RGBA8;
        uint64_t bytes = 0;
        uint64_t lastUse = 0;
    };

    bool IsBackendReady(std::string &error) const;
    uint64_t EstimateTextureBytes(const TextureDesc &desc) const;
    uint64_t TrimToBudget(uint64_t incomingBytes,
                          const iTVPTexture2D *protectedSource);
    void DestroyRecord(Record &record);

    Backend *backend_ = nullptr;
    TextureCacheLimits limits_;
    TextureCacheStats stats_;
    uint64_t useSequence_ = 0;
    std::unordered_map<const iTVPTexture2D *, Record> records_;
};

} // namespace krkr::render::sdlgpu::tvp
