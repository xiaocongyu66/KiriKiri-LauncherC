#include "../RenderManager.h"
#include "BgfxRuntime.h"
#include "DebugIntf.h"
#include "MsgIntf.h"

class tTVPBgfxVulkanRenderManager : public iTVPRenderManager {
public:
    tTVPBgfxVulkanRenderManager() : Software(TVPGetSoftwareRenderManager()) {
        TVPBgfx::InitializeVulkan(1, 1);
    }

    ~tTVPBgfxVulkanRenderManager() override { TVPBgfx::Shutdown(); }

    bool IsSoftware() override { return true; }
    const char *GetName() override { return "bgfx Vulkan"; }

    iTVPTexture2D *CreateTexture2D(const void *pixel, int pitch, unsigned int w,
                                   unsigned int h, TVPTextureFormat::e format,
                                   int flags = RENDER_CREATE_TEXTURE_FLAG_ANY) override {
        return Software->CreateTexture2D(pixel, pitch, w, h, format, flags);
    }

    iTVPTexture2D *CreateTexture2D(tTVPBitmap *bmp) override {
        return Software->CreateTexture2D(bmp);
    }

    iTVPTexture2D *CreateTexture2D(TJS::tTJSBinaryStream *stream) override {
        return Software->CreateTexture2D(stream);
    }

    iTVPTexture2D *CreateTexture2D(unsigned int neww, unsigned int newh,
                                   iTVPTexture2D *tex) override {
        return Software->CreateTexture2D(neww, newh, tex);
    }

    iTVPRenderMethod *GetRenderMethod(const char *name, uint32_t *hint = nullptr) override {
        return Software->GetRenderMethod(name, hint);
    }

    bool GetRenderStat(unsigned int &drawCount, uint64_t &vmemsize) override {
        return Software->GetRenderStat(drawCount, vmemsize);
    }

    void BeginStencil(iTVPTexture2D *reftex) override { Software->BeginStencil(reftex); }
    void EndStencil() override { Software->EndStencil(); }
    void SetRenderTarget(iTVPTexture2D *target) override { Software->SetRenderTarget(target); }

    int EnumParameterID(const char *name) override { return Software->EnumParameterID(name); }
    void SetParameterUInt(int id, unsigned int value) override { Software->SetParameterUInt(id, value); }
    void SetParameterInt(int id, int value) override { Software->SetParameterInt(id, value); }
    void SetParameterPtr(int id, const void *value) override { Software->SetParameterPtr(id, value); }
    void SetParameterFloat(int id, float value) override { Software->SetParameterFloat(id, value); }

    void OperateRect(iTVPRenderMethod *method, iTVPTexture2D *target,
                     iTVPTexture2D *refTarget, const tTVPRect &targetRect,
                     const tRenderTexRectArray &textures) override {
        Software->OperateRect(method, target, refTarget, targetRect, textures);
    }

    void OperateTriangles(iTVPRenderMethod *method, int nTriangles,
                          iTVPTexture2D *target, iTVPTexture2D *refTarget,
                          const tTVPRect &clipRect, const tTVPPointD *targetPoints,
                          const tRenderTexQuadArray &textures) override {
        Software->OperateTriangles(method, nTriangles, target, refTarget, clipRect, targetPoints, textures);
    }

    void OperatePerspective(iTVPRenderMethod *method, int nQuads,
                            iTVPTexture2D *target, iTVPTexture2D *refTarget,
                            const tTVPRect &clipRect, const tTVPPointD *targetPoints,
                            const tRenderTexQuadArray &textures) override {
        Software->OperatePerspective(method, nQuads, target, refTarget, clipRect, targetPoints, textures);
    }

private:
    iTVPRenderManager *Software = nullptr;
};

static iTVPRenderManager *TVPCreateBgfxVulkanRenderManager() {
    return new tTVPBgfxVulkanRenderManager();
}

static class __TVPBgfxVulkanRenderManagerAutoRegister {
public:
    __TVPBgfxVulkanRenderManagerAutoRegister() {
        TVPRegisterRenderManager("vulkan", TVPCreateBgfxVulkanRenderManager);
        TVPRegisterRenderManager("bgfx-vk", TVPCreateBgfxVulkanRenderManager);
    }
} __TVPBgfxVulkanRenderManagerAutoRegister_instance;
