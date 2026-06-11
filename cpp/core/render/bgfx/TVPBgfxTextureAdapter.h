#pragma once

#include "../../visual/RenderManager.h"

#include <vector>

namespace TVPBgfxAdapter {

iTVPTexture2D *WrapTexture(iTVPTexture2D *software,
                           const void *pixel = nullptr, int pitch = 0,
                           TVPTextureFormat::e format = TVPTextureFormat::None);
iTVPTexture2D *UnwrapTexture(iTVPTexture2D *texture);
tRenderTexRectArray UnwrapRectTextures(
    const tRenderTexRectArray &textures,
    std::vector<tRenderTexRectArray::Element> &storage);
tRenderTexQuadArray UnwrapQuadTextures(
    const tRenderTexQuadArray &textures,
    std::vector<tRenderTexQuadArray::Element> &storage);

} // namespace TVPBgfxAdapter
