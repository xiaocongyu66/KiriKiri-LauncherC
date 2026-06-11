#include "../../visual/RenderManager.h"
#include "BgfxRuntime.h"
#include "TVPBgfxTextureAdapter.h"

#include <vector>

namespace {

class tTVPBgfxVulkanRenderManager : public iTVPRenderManager {
public:
    tTVPBgfxVulkanRenderManager() : Software(TVPGetSoftwareRenderManager()) {
        TVPBgfx::InitializeVulkan(0, 0);
    }

    ~tTVPBgfxVulkanRenderManager() override { TVPBgfx::Shutdown(); }

    bool IsSoftware() override { return true; }
    const char *GetName() override { return "bgfx Vulkan"; }

    iTVPTexture2D *CreateTexture2D(const void *pixel, int pitch, unsigned int w,
                                   unsigned int h, TVPTextureFormat::e format,
                                   int flags = RENDER_CREATE_TEXTURE_FLAG_ANY) override {
        return TVPBgfxAdapter::WrapTexture(Software->CreateTexture2D(pixel, pitch, w, h, format, flags),
                               pixel, pitch, format);
    }

    iTVPTexture2D *CreateTexture2D(tTVPBitmap *bmp) override {
        return TVPBgfxAdapter::WrapTexture(Software->CreateTexture2D(bmp));
    }

    iTVPTexture2D *CreateTexture2D(TJS::tTJSBinaryStream *stream) override {
        return TVPBgfxAdapter::WrapTexture(Software->CreateTexture2D(stream));
    }

    iTVPTexture2D *CreateTexture2D(unsigned int neww, unsigned int newh,
                                   iTVPTexture2D *tex) override {
        return TVPBgfxAdapter::WrapTexture(
            Software->CreateTexture2D(neww, newh, TVPBgfxAdapter::UnwrapTexture(tex)));
    }

    iTVPRenderMethod *GetRenderMethod(const char *name, uint32_t *hint = nullptr) override {
        return Software->GetRenderMethod(name, hint);
    }

    bool GetRenderStat(unsigned int &drawCount, uint64_t &vmemsize) override {
        return Software->GetRenderStat(drawCount, vmemsize);
    }

    bool GetTextureStat(iTVPTexture2D *texture, uint64_t &vmemsize) override {
        return Software->GetTextureStat(TVPBgfxAdapter::UnwrapTexture(texture), vmemsize);
    }

    void BeginStencil(iTVPTexture2D *reftex) override {
        Software->BeginStencil(TVPBgfxAdapter::UnwrapTexture(reftex));
    }
    void EndStencil() override { Software->EndStencil(); }
    void SetRenderTarget(iTVPTexture2D *target) override {
        Software->SetRenderTarget(TVPBgfxAdapter::UnwrapTexture(target));
    }

    int EnumParameterID(const char *name) override { return Software->EnumParameterID(name); }
    void SetParameterUInt(int id, unsigned int value) override { Software->SetParameterUInt(id, value); }
    void SetParameterInt(int id, int value) override { Software->SetParameterInt(id, value); }
    void SetParameterPtr(int id, const void *value) override { Software->SetParameterPtr(id, value); }
    void SetParameterFloat(int id, float value) override { Software->SetParameterFloat(id, value); }

    void OperateRect(iTVPRenderMethod *method, iTVPTexture2D *target,
                     iTVPTexture2D *refTarget, const tTVPRect &targetRect,
                     const tRenderTexRectArray &textures) override {
        const char *methodName = method ? method->GetName().c_str() : "";
        TVPBgfx::StageRectBatch(methodName, targetRect.left,
                                targetRect.top, targetRect.get_width(),
                                targetRect.get_height(),
                                static_cast<uint32_t>(textures.size()));
        std::vector<tRenderTexRectArray::Element> unwrappedStorage;
        auto unwrappedTextures = TVPBgfxAdapter::UnwrapRectTextures(textures, unwrappedStorage);
        Software->OperateRect(method, TVPBgfxAdapter::UnwrapTexture(target),
                              TVPBgfxAdapter::UnwrapTexture(refTarget), targetRect,
                              unwrappedTextures);
    }

    void OperateTriangles(iTVPRenderMethod *method, int nTriangles,
                          iTVPTexture2D *target, iTVPTexture2D *refTarget,
                          const tTVPRect &clipRect, const tTVPPointD *targetPoints,
                          const tRenderTexQuadArray &textures) override {
        if(nTriangles > 0 && targetPoints && clipRect.get_width() > 0 &&
           clipRect.get_height() > 0) {
            const int pointCount = nTriangles * 3;
            std::vector<double> points(static_cast<size_t>(pointCount) * 2);
            for(int i = 0; i < pointCount; ++i) {
                points[static_cast<size_t>(i) * 2 + 0] = targetPoints[i].x;
                points[static_cast<size_t>(i) * 2 + 1] = targetPoints[i].y;
            }
            const char *methodName = method ? method->GetName().c_str() : "";
            TVPBgfx::StageTriangleBatch(
                methodName, static_cast<uint32_t>(nTriangles), clipRect.left,
                clipRect.top, clipRect.get_width(), clipRect.get_height(),
                points.data());
        }
        std::vector<tRenderTexQuadArray::Element> unwrappedStorage;
        auto unwrappedTextures = TVPBgfxAdapter::UnwrapQuadTextures(textures, unwrappedStorage);
        Software->OperateTriangles(method, nTriangles, TVPBgfxAdapter::UnwrapTexture(target),
                                   TVPBgfxAdapter::UnwrapTexture(refTarget), clipRect,
                                   targetPoints, unwrappedTextures);
    }

    void OperatePerspective(iTVPRenderMethod *method, int nQuads,
                            iTVPTexture2D *target, iTVPTexture2D *refTarget,
                            const tTVPRect &clipRect, const tTVPPointD *targetPoints,
                            const tRenderTexQuadArray &textures) override {
        std::vector<tRenderTexQuadArray::Element> unwrappedStorage;
        auto unwrappedTextures = TVPBgfxAdapter::UnwrapQuadTextures(textures, unwrappedStorage);
        Software->OperatePerspective(method, nQuads, TVPBgfxAdapter::UnwrapTexture(target),
                                     TVPBgfxAdapter::UnwrapTexture(refTarget), clipRect,
                                     targetPoints, unwrappedTextures);
    }

private:
    iTVPRenderManager *Software = nullptr;
};

static iTVPRenderManager *TVPCreateBgfxVulkanRenderManager() {
    return new tTVPBgfxVulkanRenderManager();
}

void TVPRegisterBgfxVulkanRenderManager() {
    TVPRegisterRenderManager("vulkan", TVPCreateBgfxVulkanRenderManager);
    TVPRegisterRenderManager("bgfx-vk", TVPCreateBgfxVulkanRenderManager);
}

} // namespace
