#include "SDLGpuTvpAdapter.h"

#include <limits>
#include <vector>

namespace krkr::render::sdlgpu::tvp {

namespace {

bool IsRgbSource(TVPTextureFormat::e format) {
    return format == TVPTextureFormat::RGB;
}

TVPTextureFormat::e ResolveReadableFormat(iTVPTexture2D &source) {
    const TVPTextureFormat::e readFormat = source.GetPixelDataFormat();
    return readFormat == TVPTextureFormat::None ? source.GetFormat() : readFormat;
}

bool NormalizeRect(iTVPTexture2D &source, const tTVPRect *requested,
                   tTVPRect &normalized, std::string &error) {
    const int sourceWidth = static_cast<int>(source.GetWidth());
    const int sourceHeight = static_cast<int>(source.GetHeight());
    if(sourceWidth <= 0 || sourceHeight <= 0) {
        error = "TVP texture has invalid size";
        return false;
    }

    if(requested) {
        normalized = *requested;
    } else {
        normalized = tTVPRect(0, 0, sourceWidth, sourceHeight);
    }

    if(normalized.left < 0 || normalized.top < 0 ||
       normalized.right > sourceWidth || normalized.bottom > sourceHeight ||
       normalized.left >= normalized.right || normalized.top >= normalized.bottom) {
        error = "TVP upload rectangle is out of bounds";
        return false;
    }

    return true;
}

bool CheckedUploadBytes(uint32_t width, uint32_t height, uint32_t bytesPerPixel,
                        uint64_t &bytes) {
    const uint64_t rowBytes = static_cast<uint64_t>(width) * bytesPerPixel;
    if(rowBytes > std::numeric_limits<uint32_t>::max()) {
        bytes = 0;
        return false;
    }
    bytes = rowBytes * height;
    return bytes <= std::numeric_limits<uint32_t>::max();
}

void ExpandRgbToRgba(const uint8_t *source, uint8_t *destination,
                     uint32_t pixelCount) {
    for(uint32_t index = 0; index < pixelCount; ++index) {
        destination[index * 4 + 0] = source[index * 3 + 0];
        destination[index * 4 + 1] = source[index * 3 + 1];
        destination[index * 4 + 2] = source[index * 3 + 2];
        destination[index * 4 + 3] = 0xff;
    }
}

} // namespace

bool IsSupportedSourceFormat(TVPTextureFormat::e format) {
    switch(format) {
        case TVPTextureFormat::Gray:
        case TVPTextureFormat::RGB:
        case TVPTextureFormat::RGBA:
            return true;
        case TVPTextureFormat::None:
        case TVPTextureFormat::Compressed:
        case TVPTextureFormat::CompressedEnd:
            break;
    }
    return false;
}

uint32_t SourceBytesPerPixel(TVPTextureFormat::e format) {
    switch(format) {
        case TVPTextureFormat::Gray:
            return 1;
        case TVPTextureFormat::RGB:
            return 3;
        case TVPTextureFormat::RGBA:
            return 4;
        case TVPTextureFormat::None:
        case TVPTextureFormat::Compressed:
        case TVPTextureFormat::CompressedEnd:
            break;
    }
    return 0;
}

PixelFormat ToUploadPixelFormat(TVPTextureFormat::e format) {
    switch(format) {
        case TVPTextureFormat::Gray:
            return PixelFormat::R8;
        case TVPTextureFormat::RGB:
        case TVPTextureFormat::RGBA:
        default:
            return PixelFormat::RGBA8;
    }
}

bool MakeTextureDesc(iTVPTexture2D &source, TextureDesc &desc,
                     std::string &error, const char *debugName) {
    const TVPTextureFormat::e format = ResolveReadableFormat(source);
    if(!IsSupportedSourceFormat(format)) {
        error = "unsupported TVP texture format for SDL_GPU upload";
        return false;
    }

    const uint32_t width = source.GetWidth();
    const uint32_t height = source.GetHeight();
    uint64_t ignoredBytes = 0;
    const uint32_t uploadBytesPerPixel =
        IsRgbSource(format) ? 4 : SourceBytesPerPixel(format);
    if(!CheckedUploadBytes(width, height,
                           uploadBytesPerPixel, ignoredBytes)) {
        error = "TVP texture is too large for SDL_GPU upload";
        return false;
    }

    desc = {};
    desc.width = width;
    desc.height = height;
    desc.format = ToUploadPixelFormat(format);
    desc.renderTarget = false;
    desc.debugName = debugName;
    error.clear();
    return true;
}

TextureHandle CreateTexture2D(Backend &backend, iTVPTexture2D &source,
                              const char *debugName, std::string *error) {
    TextureDesc desc;
    std::string localError;
    if(!MakeTextureDesc(source, desc, localError, debugName)) {
        if(error)
            *error = localError;
        return 0;
    }

    TextureHandle handle = backend.CreateTexture2D(desc);
    if(!handle && error)
        *error = backend.LastError();
    return handle;
}

UploadResult UploadTexture2D(Backend &backend, TextureHandle handle,
                             iTVPTexture2D &source,
                             const tTVPRect *sourceRect) {
    UploadResult result;

    if(!handle) {
        result.error = "SDL_GPU texture handle is invalid";
        return result;
    }

    const TVPTextureFormat::e format = ResolveReadableFormat(source);
    if(!IsSupportedSourceFormat(format)) {
        result.error = "unsupported TVP texture format for SDL_GPU upload";
        return result;
    }

    tTVPRect rect;
    if(!NormalizeRect(source, sourceRect, rect, result.error)) {
        return result;
    }

    const uint32_t width = static_cast<uint32_t>(rect.get_width());
    const uint32_t height = static_cast<uint32_t>(rect.get_height());
    const uint32_t sourceBytesPerPixel = SourceBytesPerPixel(format);
    const uint32_t uploadBytesPerPixel = IsRgbSource(format) ? 4 : sourceBytesPerPixel;
    uint64_t uploadBytes = 0;
    if(!CheckedUploadBytes(width, height, uploadBytesPerPixel, uploadBytes)) {
        result.error = "TVP upload rectangle is too large";
        return result;
    }

    const auto *firstRow =
        static_cast<const uint8_t *>(source.GetScanLineForRead(rect.top));
    if(!firstRow) {
        result.error = "TVP texture scanline is unavailable";
        return result;
    }

    const int sourcePitch = source.GetPitch();
    if(sourcePitch <= 0 ||
       static_cast<uint64_t>(sourcePitch) <
           static_cast<uint64_t>(source.GetWidth()) * sourceBytesPerPixel) {
        result.error = "TVP texture pitch is invalid";
        return result;
    }

    if(!IsRgbSource(format)) {
        const uint8_t *pixels =
            firstRow + static_cast<size_t>(rect.left) * sourceBytesPerPixel;
        result.uploaded = backend.UploadTexture2D(
            handle, pixels, static_cast<uint32_t>(sourcePitch),
            static_cast<uint32_t>(rect.left), static_cast<uint32_t>(rect.top),
            width, height);
        if(!result.uploaded)
            result.error = backend.LastError();
        result.uploadBytes = result.uploaded ? uploadBytes : 0;
        return result;
    }

    std::vector<uint8_t> expanded(static_cast<size_t>(uploadBytes));
    const uint32_t rowBytes = width * uploadBytesPerPixel;
    for(uint32_t row = 0; row < height; ++row) {
        const auto *sourceRow = static_cast<const uint8_t *>(
            source.GetScanLineForRead(rect.top + row));
        if(!sourceRow) {
            result.error = "TVP texture scanline is unavailable";
            return result;
        }
        sourceRow += static_cast<size_t>(rect.left) * sourceBytesPerPixel;
        ExpandRgbToRgba(sourceRow, expanded.data() + row * rowBytes, width);
    }

    result.uploaded = backend.UploadTexture2D(
        handle, expanded.data(), rowBytes, static_cast<uint32_t>(rect.left),
        static_cast<uint32_t>(rect.top), width, height);
    if(!result.uploaded)
        result.error = backend.LastError();
    result.converted = result.uploaded;
    result.uploadBytes = result.uploaded ? uploadBytes : 0;
    return result;
}

} // namespace krkr::render::sdlgpu::tvp
