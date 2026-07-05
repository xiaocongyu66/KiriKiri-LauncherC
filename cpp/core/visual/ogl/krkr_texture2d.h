#pragma once

// Transitional texture boundary for the renderer migration.
//
// The legacy Cocos host still consumes adapter textures directly through
// cocos2d::Sprite::setTexture().  Keep this header as the only Cocos-facing
// compatibility point while core renderer interfaces move to krkr::Texture2D.
// Once the Flutter + SDL3 host owns presentation fully, this type can be
// replaced by the standalone AetherKiri-style Texture2D implementation.

#ifndef KRKR2_ENABLE_COCOS_HOST
#define KRKR2_ENABLE_COCOS_HOST 0
#endif

#if KRKR2_ENABLE_COCOS_HOST
#include "renderer/CCTexture2D.h"
#include "renderer/CCGLProgram.h"
#include "renderer/CCGLProgramCache.h"
#else
#include <cstddef>
#endif

namespace krkr {

#ifndef KRKR_TEXTURE2D_ALIAS_DECLARED
#define KRKR_TEXTURE2D_ALIAS_DECLARED 1
#if KRKR2_ENABLE_COCOS_HOST
using Texture2D = cocos2d::Texture2D;
#else
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
#endif
#if !KRKR2_ENABLE_COCOS_HOST
using PixelFormat = Texture2D::PixelFormat;
#else
using PixelFormat = cocos2d::Texture2D::PixelFormat;
using Size = cocos2d::Size;
#endif

inline void SetDefaultTextureProgram(Texture2D *texture) {
#if KRKR2_ENABLE_COCOS_HOST
    if(!texture)
        return;
    texture->setGLProgram(cocos2d::GLProgramCache::getInstance()->getGLProgram(
        cocos2d::GLProgram::SHADER_NAME_POSITION_TEXTURE));
#else
    (void)texture;
#endif
}

} // namespace krkr
