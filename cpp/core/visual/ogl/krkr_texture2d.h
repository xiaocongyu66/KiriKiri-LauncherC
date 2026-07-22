#pragma once

// Standalone texture type for the SDL3 / Flutter present path.
// Cocos2d Texture2D alias was removed with the Cocos host.

#include <cstddef>

namespace krkr {

#ifndef KRKR_TEXTURE2D_ALIAS_DECLARED
#define KRKR_TEXTURE2D_ALIAS_DECLARED 1
struct Size {
    float width = 0.0f;
    float height = 0.0f;

    constexpr Size() = default;
    constexpr Size(float w, float h) : width(w), height(h) {}

    static const Size ZERO;
};

inline const Size Size::ZERO{};

class Texture2D {
public:
    enum class PixelFormat { RGBA8888 };

    virtual ~Texture2D() = default;

    Texture2D *autorelease() { return this; }
    bool initWithData(const void *, std::size_t, PixelFormat, int w, int h,
                      const Size &) {
        _pixelsWide = w;
        _pixelsHigh = h;
        return true;
    }
    void updateWithData(const void *, int, int, int, int) {}
    int getPixelsWide() const { return _pixelsWide; }
    int getPixelsHigh() const { return _pixelsHigh; }

protected:
    unsigned int _name = 0;
    Size _contentSize{};
    float _maxS = 1.0f;
    float _maxT = 1.0f;
    int _pixelsWide = 0;
    int _pixelsHigh = 0;
    PixelFormat _pixelFormat = PixelFormat::RGBA8888;
    bool _hasPremultipliedAlpha = false;
    bool _hasMipmaps = false;
};
#endif

using PixelFormat = Texture2D::PixelFormat;

inline void SetDefaultTextureProgram(Texture2D *texture) {
    (void)texture;
}

} // namespace krkr
