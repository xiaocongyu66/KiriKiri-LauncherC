#include "SDLGpuBackend.h"

#include <SDL3/SDL.h>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>

namespace krkr::render::sdlgpu {

namespace {

constexpr TextureHandle INVALID_TEXTURE_HANDLE = 0;

SDL_GPUTextureFormat ToGpuTextureFormat(PixelFormat format) {
    switch(format) {
        case PixelFormat::R8:
            return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        case PixelFormat::BGRA8:
            return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        case PixelFormat::RGBA8:
        default:
            return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }
}

uint32_t BytesPerPixel(PixelFormat format) {
    switch(format) {
        case PixelFormat::R8:
            return 1;
        case PixelFormat::BGRA8:
        case PixelFormat::RGBA8:
        default:
            return 4;
    }
}

bool CheckedTextureBytes(const TextureDesc &desc, uint64_t &bytes) {
    const uint32_t bytesPerPixel = BytesPerPixel(desc.format);
    if(desc.width == 0 || desc.height == 0) {
        bytes = 0;
        return false;
    }
    const uint64_t rowBytes = static_cast<uint64_t>(desc.width) * bytesPerPixel;
    if(rowBytes > std::numeric_limits<uint32_t>::max()) {
        bytes = 0;
        return false;
    }
    bytes = rowBytes * desc.height;
    return bytes <= std::numeric_limits<uint32_t>::max();
}

std::string SdlErrorString(const char *prefix) {
    const char *error = SDL_GetError();
    std::string message(prefix ? prefix : "SDL_GPU error");
    if(error && *error) {
        message += ": ";
        message += error;
    }
    return message;
}

bool IsTruthyEnv(const char *name) {
    const char *value = SDL_getenv(name);
    return value && *value && std::strcmp(value, "0") != 0 &&
        std::strcmp(value, "false") != 0 &&
        std::strcmp(value, "FALSE") != 0;
}

SDL_GPUShaderFormat PreferredShaderFormats() {
#if defined(__ANDROID__)
    return SDL_GPU_SHADERFORMAT_SPIRV;
#else
    return SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC |
        SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB |
        SDL_GPU_SHADERFORMAT_MSL;
#endif
}

bool UseRelaxedDeviceFeatures() {
#if defined(__ANDROID__)
    return !IsTruthyEnv("KRKR2_SDL_GPU_STRICT_FEATURES");
#else
    return IsTruthyEnv("KRKR2_SDL_GPU_RELAXED_FEATURES");
#endif
}

const char *DeviceFeatureProfileName() {
    return UseRelaxedDeviceFeatures() ? "relaxed" : "strict";
}

std::string ShaderFormatNames(SDL_GPUShaderFormat shaderFormats) {
    std::string names;
    auto append = [&names](const char *name) {
        if(!names.empty())
            names += ",";
        names += name;
    };
    if(shaderFormats & SDL_GPU_SHADERFORMAT_SPIRV)
        append("spirv");
    if(shaderFormats & SDL_GPU_SHADERFORMAT_DXBC)
        append("dxbc");
    if(shaderFormats & SDL_GPU_SHADERFORMAT_DXIL)
        append("dxil");
    if(shaderFormats & SDL_GPU_SHADERFORMAT_METALLIB)
        append("metallib");
    if(shaderFormats & SDL_GPU_SHADERFORMAT_MSL)
        append("msl");
    return names.empty() ? "none" : names;
}

void SetShaderFormatProperties(SDL_PropertiesID props,
                               SDL_GPUShaderFormat shaderFormats) {
    if(shaderFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
        SDL_SetBooleanProperty(
            props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    }
    if(shaderFormats & SDL_GPU_SHADERFORMAT_DXBC) {
        SDL_SetBooleanProperty(
            props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXBC_BOOLEAN, true);
    }
    if(shaderFormats & SDL_GPU_SHADERFORMAT_DXIL) {
        SDL_SetBooleanProperty(
            props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
    }
    if(shaderFormats & SDL_GPU_SHADERFORMAT_METALLIB) {
        SDL_SetBooleanProperty(
            props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_METALLIB_BOOLEAN, true);
    }
    if(shaderFormats & SDL_GPU_SHADERFORMAT_MSL) {
        SDL_SetBooleanProperty(
            props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
    }
}

SDL_GPUDevice *CreateCompatibleGPUDevice(SDL_GPUShaderFormat shaderFormats,
                                         bool debugMode,
                                         const char *preferredDriver) {
    SDL_PropertiesID props = SDL_CreateProperties();
    if(!props)
        return nullptr;

    SetShaderFormatProperties(props, shaderFormats);
    SDL_SetBooleanProperty(props,
                           SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,
                           debugMode);
    if(debugMode) {
        SDL_SetBooleanProperty(props,
                               SDL_PROP_GPU_DEVICE_CREATE_VERBOSE_BOOLEAN,
                               true);
    }
    if(preferredDriver && *preferredDriver) {
        SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, preferredDriver,
                                SDL_HINT_OVERRIDE);
        SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,
                              preferredDriver);
    }

    if(UseRelaxedDeviceFeatures()) {
        SDL_SetBooleanProperty(
            props,
            SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(
            props,
            SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
        SDL_SetBooleanProperty(
            props,
            SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN,
            false);
        SDL_SetBooleanProperty(
            props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN,
            false);
    }

    SDL_GPUDevice *device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    return device;
}

} // namespace

struct Backend::Impl {
    struct TextureRecord {
        SDL_GPUTexture *texture = nullptr;
        TextureDesc desc;
        uint64_t bytes = 0;
    };

    SDL_GPUDevice *device = nullptr;
    std::unordered_map<TextureHandle, TextureRecord> textures;
    TextureHandle nextHandle = 1;
    TextureStats stats;
    std::string driverName;
    std::string lastError;
};

Backend::Backend() : impl_(std::make_unique<Impl>()) {}

Backend::~Backend() { Shutdown(); }

Backend::Backend(Backend &&) noexcept = default;

Backend &Backend::operator=(Backend &&other) noexcept {
    if(this != &other) {
        Shutdown();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool Backend::Initialize(const char *preferredDriver, bool debugMode) {
    if(!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    Shutdown();

    const SDL_GPUShaderFormat shaderFormats = PreferredShaderFormats();

    impl_->device =
        CreateCompatibleGPUDevice(shaderFormats, debugMode, preferredDriver);
    if(!impl_->device) {
        impl_->lastError =
            SdlErrorString("SDL_CreateGPUDeviceWithProperties failed");
        impl_->lastError += " driver=";
        impl_->lastError +=
            preferredDriver && *preferredDriver ? preferredDriver : "(auto)";
        impl_->lastError += " shaders=";
        impl_->lastError += ShaderFormatNames(shaderFormats);
        impl_->lastError += " features=";
        impl_->lastError += DeviceFeatureProfileName();
        return false;
    }

    const char *driver = SDL_GetGPUDeviceDriver(impl_->device);
    impl_->driverName = driver ? driver : "";
    impl_->lastError.clear();
    return true;
}

void Backend::Shutdown() {
    if(!impl_) {
        return;
    }

    if(impl_->device) {
        SDL_WaitForGPUIdle(impl_->device);
        for(auto &entry : impl_->textures) {
            if(entry.second.texture) {
                SDL_ReleaseGPUTexture(impl_->device, entry.second.texture);
            }
        }
        impl_->textures.clear();
        SDL_DestroyGPUDevice(impl_->device);
    } else {
        impl_->textures.clear();
    }

    impl_->device = nullptr;
    impl_->driverName.clear();
    impl_->stats = {};
    impl_->nextHandle = 1;
}

bool Backend::IsReady() const { return impl_ && impl_->device != nullptr; }

const char *Backend::DriverName() const {
    return impl_ ? impl_->driverName.c_str() : "";
}

const std::string &Backend::LastError() const {
    static const std::string empty;
    return impl_ ? impl_->lastError : empty;
}

TextureStats Backend::Stats() const { return impl_ ? impl_->stats : TextureStats{}; }

SDL_GPUDevice *Backend::Device() const { return impl_ ? impl_->device : nullptr; }

TextureHandle Backend::CreateTexture2D(const TextureDesc &desc) {
    if(!impl_) {
        return INVALID_TEXTURE_HANDLE;
    }
    if(!IsReady()) {
        impl_->lastError = "SDL_GPU backend is not initialized";
        return INVALID_TEXTURE_HANDLE;
    }

    uint64_t estimatedBytes = 0;
    if(!CheckedTextureBytes(desc, estimatedBytes)) {
        impl_->lastError = "invalid or too large SDL_GPU texture size";
        return INVALID_TEXTURE_HANDLE;
    }

    SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if(desc.renderTarget) {
        usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    }

    const SDL_GPUTextureFormat format = ToGpuTextureFormat(desc.format);
    if(!SDL_GPUTextureSupportsFormat(impl_->device, format,
                                     SDL_GPU_TEXTURETYPE_2D, usage)) {
        impl_->lastError = "SDL_GPU texture format is not supported";
        return INVALID_TEXTURE_HANDLE;
    }

    SDL_GPUTextureCreateInfo createInfo;
    SDL_zero(createInfo);
    createInfo.type = SDL_GPU_TEXTURETYPE_2D;
    createInfo.format = format;
    createInfo.usage = usage;
    createInfo.width = desc.width;
    createInfo.height = desc.height;
    createInfo.layer_count_or_depth = 1;
    createInfo.num_levels = 1;
    createInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_PropertiesID props = 0;
    if(desc.debugName && *desc.debugName) {
        props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING,
                              desc.debugName);
        createInfo.props = props;
    }

    SDL_GPUTexture *texture = SDL_CreateGPUTexture(impl_->device, &createInfo);
    if(props) {
        SDL_DestroyProperties(props);
    }

    if(!texture) {
        impl_->lastError = SdlErrorString("SDL_CreateGPUTexture failed");
        return INVALID_TEXTURE_HANDLE;
    }

    const TextureHandle handle = impl_->nextHandle++;
    impl_->textures.emplace(handle, Impl::TextureRecord{texture, desc, estimatedBytes});
    impl_->stats.textureCount += 1;
    impl_->stats.textureBytes += estimatedBytes;
    impl_->lastError.clear();
    return handle;
}

bool Backend::UploadTexture2D(TextureHandle handle, const void *pixels,
                              uint32_t pitch, uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height) {
    if(!impl_) {
        return false;
    }
    if(!IsReady()) {
        impl_->lastError = "SDL_GPU backend is not initialized";
        return false;
    }
    if(!pixels) {
        impl_->lastError = "SDL_GPU upload source is null";
        return false;
    }

    auto textureIt = impl_->textures.find(handle);
    if(textureIt == impl_->textures.end() || !textureIt->second.texture) {
        impl_->lastError = "SDL_GPU texture handle is invalid";
        return false;
    }

    const Impl::TextureRecord &record = textureIt->second;
    if(width == 0) {
        width = record.desc.width;
    }
    if(height == 0) {
        height = record.desc.height;
    }
    if(x > record.desc.width || y > record.desc.height ||
       width > record.desc.width - x || height > record.desc.height - y) {
        impl_->lastError = "SDL_GPU upload rectangle is out of texture bounds";
        return false;
    }

    const uint32_t bytesPerPixel = BytesPerPixel(record.desc.format);
    const uint64_t rowBytes64 = static_cast<uint64_t>(width) * bytesPerPixel;
    if(rowBytes64 > std::numeric_limits<uint32_t>::max()) {
        impl_->lastError = "SDL_GPU upload row is too large";
        return false;
    }
    const uint32_t rowBytes = static_cast<uint32_t>(rowBytes64);
    if(pitch < rowBytes) {
        impl_->lastError = "SDL_GPU upload pitch is smaller than row size";
        return false;
    }

    const uint64_t dataBytes64 = static_cast<uint64_t>(rowBytes) * height;
    if(dataBytes64 > std::numeric_limits<uint32_t>::max()) {
        impl_->lastError = "SDL_GPU upload data is too large";
        return false;
    }
    const uint32_t dataBytes = static_cast<uint32_t>(dataBytes64);

    SDL_GPUTransferBufferCreateInfo transferInfo;
    SDL_zero(transferInfo);
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = dataBytes;

    SDL_GPUTransferBuffer *transferBuffer =
        SDL_CreateGPUTransferBuffer(impl_->device, &transferInfo);
    if(!transferBuffer) {
        impl_->lastError = SdlErrorString("SDL_CreateGPUTransferBuffer failed");
        return false;
    }

    uint8_t *mapped = static_cast<uint8_t *>(
        SDL_MapGPUTransferBuffer(impl_->device, transferBuffer, false));
    if(!mapped) {
        SDL_ReleaseGPUTransferBuffer(impl_->device, transferBuffer);
        impl_->lastError = SdlErrorString("SDL_MapGPUTransferBuffer failed");
        return false;
    }

    const uint8_t *source = static_cast<const uint8_t *>(pixels);
    if(pitch == rowBytes) {
        std::memcpy(mapped, source, dataBytes);
    } else {
        for(uint32_t row = 0; row < height; ++row) {
            std::memcpy(mapped + static_cast<size_t>(row) * rowBytes,
                        source + static_cast<size_t>(row) * pitch, rowBytes);
        }
    }
    SDL_UnmapGPUTransferBuffer(impl_->device, transferBuffer);

    SDL_GPUCommandBuffer *commandBuffer = SDL_AcquireGPUCommandBuffer(impl_->device);
    if(!commandBuffer) {
        SDL_ReleaseGPUTransferBuffer(impl_->device, transferBuffer);
        impl_->lastError = SdlErrorString("SDL_AcquireGPUCommandBuffer failed");
        return false;
    }

    SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    if(!copyPass) {
        SDL_CancelGPUCommandBuffer(commandBuffer);
        SDL_ReleaseGPUTransferBuffer(impl_->device, transferBuffer);
        impl_->lastError = SdlErrorString("SDL_BeginGPUCopyPass failed");
        return false;
    }

    SDL_GPUTextureTransferInfo sourceInfo;
    SDL_zero(sourceInfo);
    sourceInfo.transfer_buffer = transferBuffer;
    sourceInfo.rows_per_layer = height;
    sourceInfo.pixels_per_row = width;

    SDL_GPUTextureRegion destination;
    SDL_zero(destination);
    destination.texture = record.texture;
    destination.x = x;
    destination.y = y;
    destination.w = width;
    destination.h = height;
    destination.d = 1;

    SDL_UploadToGPUTexture(copyPass, &sourceInfo, &destination, false);
    SDL_EndGPUCopyPass(copyPass);

    const bool submitted = SDL_SubmitGPUCommandBuffer(commandBuffer);
    SDL_ReleaseGPUTransferBuffer(impl_->device, transferBuffer);
    if(!submitted) {
        impl_->lastError = SdlErrorString("SDL_SubmitGPUCommandBuffer failed");
        return false;
    }

    impl_->stats.uploadBytes += dataBytes;
    impl_->lastError.clear();
    return true;
}

void Backend::DestroyTexture(TextureHandle handle) {
    if(!impl_ || !impl_->device) {
        return;
    }

    auto textureIt = impl_->textures.find(handle);
    if(textureIt == impl_->textures.end()) {
        return;
    }

    SDL_ReleaseGPUTexture(impl_->device, textureIt->second.texture);
    impl_->stats.textureCount =
        impl_->stats.textureCount > 0 ? impl_->stats.textureCount - 1 : 0;
    impl_->stats.textureBytes =
        impl_->stats.textureBytes >= textureIt->second.bytes
            ? impl_->stats.textureBytes - textureIt->second.bytes
            : 0;
    impl_->textures.erase(textureIt);
}

SDL_GPUTexture *Backend::NativeTexture(TextureHandle handle) const {
    if(!impl_) {
        return nullptr;
    }
    auto textureIt = impl_->textures.find(handle);
    if(textureIt == impl_->textures.end()) {
        return nullptr;
    }
    return textureIt->second.texture;
}

} // namespace krkr::render::sdlgpu
