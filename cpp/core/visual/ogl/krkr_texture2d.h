#pragma once

// Transitional texture boundary for the renderer migration.
//
// The legacy Cocos host still consumes adapter textures directly through
// cocos2d::Sprite::setTexture().  Keep this header as the only Cocos-facing
// compatibility point while core renderer interfaces move to krkr::Texture2D.
// Once the Flutter + SDL3 host owns presentation fully, this type can be
// replaced by the standalone AetherKiri-style Texture2D implementation.

#include "renderer/CCTexture2D.h"
#include "renderer/CCGLProgram.h"
#include "renderer/CCGLProgramCache.h"

namespace krkr {

#ifndef KRKR_TEXTURE2D_ALIAS_DECLARED
#define KRKR_TEXTURE2D_ALIAS_DECLARED 1
using Texture2D = cocos2d::Texture2D;
#endif
using PixelFormat = cocos2d::Texture2D::PixelFormat;
using Size = cocos2d::Size;

inline void SetDefaultTextureProgram(Texture2D *texture) {
    if(!texture)
        return;
    texture->setGLProgram(cocos2d::GLProgramCache::getInstance()->getGLProgram(
        cocos2d::GLProgram::SHADER_NAME_POSITION_TEXTURE));
}

} // namespace krkr
