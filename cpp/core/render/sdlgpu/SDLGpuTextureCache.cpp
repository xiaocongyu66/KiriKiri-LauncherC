#include "SDLGpuTextureCache.h"

#include <limits>

namespace krkr::render::sdlgpu::tvp {

namespace {

uint32_t UploadBytesPerPixel(PixelFormat format) {
    switch(format) {
        case PixelFormat::R8:
            return 1;
        case PixelFormat::BGRA8:
        case PixelFormat::RGBA8:
        default:
            return 4;
    }
}

} // namespace

TextureCache::TextureCache(Backend *backend) : backend_(backend) {}

TextureCache::~TextureCache() { Clear(); }

void TextureCache::SetBackend(Backend *backend) {
    if(backend_ == backend)
        return;
    Clear();
    backend_ = backend;
}

void TextureCache::SetLimits(const TextureCacheLimits &limits) {
    limits_ = limits;
    TrimToBudget(0, nullptr);
}

const TextureCacheLimits &TextureCache::Limits() const { return limits_; }

TextureCacheStats TextureCache::Stats() const { return stats_; }

bool TextureCache::Contains(const iTVPTexture2D *source) const {
    return source && records_.find(source) != records_.end();
}

TextureCacheResult TextureCache::Upsert(iTVPTexture2D &source,
                                        const tTVPRect *sourceRect,
                                        const char *debugName) {
    TextureCacheResult result;
    if(!IsBackendReady(result.error))
        return result;

    TextureDesc desc;
    if(!MakeTextureDesc(source, desc, result.error, debugName))
        return result;

    const uint64_t textureBytes = EstimateTextureBytes(desc);
    if(textureBytes == 0) {
        result.error = "SDL_GPU texture size is invalid";
        return result;
    }
    if(textureBytes > limits_.maxSingleTextureBytes) {
        result.error = "SDL_GPU texture exceeds single texture budget";
        return result;
    }
    if(textureBytes > limits_.maxTotalTextureBytes) {
        result.error = "SDL_GPU texture exceeds total texture budget";
        return result;
    }

    Record *record = nullptr;
    auto recordIt = records_.find(&source);
    if(recordIt != records_.end()) {
        record = &recordIt->second;
        if(record->width != desc.width || record->height != desc.height ||
           record->format != desc.format) {
            DestroyRecord(*record);
            records_.erase(recordIt);
            record = nullptr;
        }
    }

    result.evicted = TrimToBudget(record ? 0 : textureBytes, &source);
    if(!record) {
        TextureHandle handle = backend_->CreateTexture2D(desc);
        if(!handle) {
            result.error = backend_->LastError();
            return result;
        }
        Record newRecord;
        newRecord.handle = handle;
        newRecord.width = desc.width;
        newRecord.height = desc.height;
        newRecord.format = desc.format;
        newRecord.bytes = textureBytes;
        newRecord.lastUse = ++useSequence_;
        auto inserted = records_.emplace(&source, newRecord);
        record = &inserted.first->second;
        stats_.textureCount++;
        stats_.textureBytes += textureBytes;
        result.created = true;
    }

    if(sourceRect) {
        if(record->hasCpuDirtyRect) {
            record->cpuDirtyRect.do_union(*sourceRect);
        } else {
            record->cpuDirtyRect = *sourceRect;
            record->hasCpuDirtyRect = true;
        }
    } else {
        record->cpuDirtyRect =
            tTVPRect(0, 0, static_cast<tjs_int>(desc.width),
                     static_cast<tjs_int>(desc.height));
        record->hasCpuDirtyRect = true;
    }

    const bool fullUpload =
        result.created || !record->gpuResident || sourceRect == nullptr;
    const tTVPRect *uploadRect =
        fullUpload || !record->hasCpuDirtyRect ? nullptr
                                               : &record->cpuDirtyRect;
    UploadResult upload = UploadTexture2D(*backend_, record->handle, source,
                                          uploadRect);
    if(!upload.uploaded) {
        result.error = upload.error;
        if(result.created) {
            Release(&source);
            result.created = false;
        }
        return result;
    }

    record->lastUse = ++useSequence_;
    if(!record->gpuResident) {
        stats_.gpuResidentBytes += record->bytes;
    }
    record->gpuResident = true;
    record->cpuResident = true;
    record->hasCpuDirtyRect = false;
    record->cpuDirtyRect.clear();
    if(fullUpload) {
        record->fullUploads++;
        stats_.fullUploads++;
    } else {
        record->partialUploads++;
        stats_.partialUploads++;
    }
    stats_.uploads++;
    stats_.uploadBytes += upload.uploadBytes;
    result.handle = record->handle;
    result.uploaded = true;
    result.converted = upload.converted;
    result.textureBytes = record->bytes;
    result.uploadBytes = upload.uploadBytes;
    return result;
}

void TextureCache::Release(const iTVPTexture2D *source) {
    if(!source)
        return;
    auto recordIt = records_.find(source);
    if(recordIt == records_.end())
        return;
    DestroyRecord(recordIt->second);
    records_.erase(recordIt);
}

void TextureCache::Clear() {
    if(backend_) {
        for(auto &entry : records_) {
            DestroyRecord(entry.second);
        }
    }
    records_.clear();
    stats_ = {};
}

bool TextureCache::IsBackendReady(std::string &error) const {
    if(!backend_) {
        error = "SDL_GPU backend is not assigned";
        return false;
    }
    if(!backend_->IsReady()) {
        error = "SDL_GPU backend is not initialized";
        return false;
    }
    return true;
}

uint64_t TextureCache::EstimateTextureBytes(const TextureDesc &desc) const {
    const uint32_t bytesPerPixel = UploadBytesPerPixel(desc.format);
    if(desc.width == 0 || desc.height == 0 || bytesPerPixel == 0)
        return 0;

    const uint64_t rowBytes = static_cast<uint64_t>(desc.width) * bytesPerPixel;
    if(rowBytes > std::numeric_limits<uint32_t>::max())
        return 0;

    const uint64_t textureBytes = rowBytes * desc.height;
    if(textureBytes > std::numeric_limits<uint32_t>::max())
        return 0;
    return textureBytes;
}

uint64_t TextureCache::TrimToBudget(uint64_t incomingBytes,
                                    const iTVPTexture2D *protectedSource) {
    uint64_t evicted = 0;
    while(stats_.textureBytes + incomingBytes > limits_.maxTotalTextureBytes &&
          !records_.empty()) {
        auto victimIt = records_.end();
        for(auto currentIt = records_.begin(); currentIt != records_.end();
            ++currentIt) {
            if(currentIt->first == protectedSource)
                continue;
            if(victimIt == records_.end() ||
               currentIt->second.lastUse < victimIt->second.lastUse) {
                victimIt = currentIt;
            }
        }
        if(victimIt == records_.end())
            break;
        DestroyRecord(victimIt->second);
        records_.erase(victimIt);
        evicted++;
    }
    stats_.evictions += evicted;
    return evicted;
}

void TextureCache::DestroyRecord(Record &record) {
    if(backend_ && record.handle)
        backend_->DestroyTexture(record.handle);
    if(stats_.textureCount > 0)
        stats_.textureCount--;
    if(record.gpuResident) {
        stats_.gpuResidentBytes = stats_.gpuResidentBytes >= record.bytes
            ? stats_.gpuResidentBytes - record.bytes
            : 0;
    }
    stats_.textureBytes =
        stats_.textureBytes >= record.bytes ? stats_.textureBytes - record.bytes
                                            : 0;
    record.handle = 0;
    record.bytes = 0;
}

} // namespace krkr::render::sdlgpu::tvp
