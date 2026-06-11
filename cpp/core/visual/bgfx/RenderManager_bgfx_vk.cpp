#include "../RenderManager.h"
#include "BgfxRuntime.h"
#include "DebugIntf.h"
#include "MsgIntf.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint16_t InvalidBgfxTextureHandle = UINT16_MAX;

class tTVPBgfxTexture2D : public iTVPTexture2D {
public:
    tTVPBgfxTexture2D(iTVPTexture2D *software, const void *pixel = nullptr,
                      int pitch = 0, TVPTextureFormat::e format = TVPTextureFormat::None) :
        iTVPTexture2D(software ? software->GetWidth() : 0,
                      software ? software->GetHeight() : 0),
        Software(software) {
        (void)pixel;
        (void)pitch;
        (void)format;
    }

    ~tTVPBgfxTexture2D() override {
        if(BgfxHandle != InvalidBgfxTextureHandle)
            TVPBgfx::DestroyTexture2D(BgfxHandle);
        if(Software)
            Software->Release();
    }

    iTVPTexture2D *GetSoftwareTexture() const { return Software; }

    void SetSize(unsigned int w, unsigned int h) override {
        iTVPTexture2D::SetSize(w, h);
        if(Software)
            Software->SetSize(w, h);
        if(BgfxHandle != InvalidBgfxTextureHandle) {
            TVPBgfx::DestroyTexture2D(BgfxHandle);
            BgfxHandle = InvalidBgfxTextureHandle;
        }
    }

    TVPTextureFormat::e GetFormat() const override {
        return Software ? Software->GetFormat() : TVPTextureFormat::None;
    }
    const void *GetScanLineForRead(tjs_uint line) override {
        return Software ? Software->GetScanLineForRead(line) : nullptr;
    }
    const void *GetPixelData() override {
        return Software ? Software->GetPixelData() : nullptr;
    }
    TVPTextureFormat::e GetPixelDataFormat() const override {
        return Software ? Software->GetPixelDataFormat() : TVPTextureFormat::None;
    }
    void *GetScanLineForWrite(tjs_uint line) override {
        return Software ? Software->GetScanLineForWrite(line) : nullptr;
    }
    tjs_int GetPitch() const override { return Software ? Software->GetPitch() : 0; }
    uint32_t GetPoint(int x, int y) override {
        return Software ? Software->GetPoint(x, y) : 0;
    }
    void SetPoint(int x, int y, uint32_t color) override {
        if(Software)
            Software->SetPoint(x, y, color);
    }
    bool IsStatic() override { return Software ? Software->IsStatic() : false; }
    bool IsOpaque() override { return Software ? Software->IsOpaque() : false; }
    cocos2d::Texture2D *GetAdapterTexture(cocos2d::Texture2D *origTex) override {
        return Software ? Software->GetAdapterTexture(origTex) : origTex;
    }
    bool GetScale(float &x, float &y) override {
        if(Software)
            return Software->GetScale(x, y);
        x = 1.0f;
        y = 1.0f;
        return false;
    }
    unsigned int GetNativeGLTextureId() const override {
        return Software ? Software->GetNativeGLTextureId() : 0;
    }
    void InvalidatePixelCache() override {
        if(Software)
            Software->InvalidatePixelCache();
    }

    void Update(const void *pixel, TVPTextureFormat::e format, int pitch,
                const tTVPRect &rect) override {
        if(Software)
            Software->Update(pixel, format, pitch, rect);
    }

private:
    iTVPTexture2D *Software = nullptr;
    uint16_t BgfxHandle = InvalidBgfxTextureHandle;
};

iTVPTexture2D *WrapBgfxTexture(iTVPTexture2D *software, const void *pixel = nullptr,
                               int pitch = 0,
                               TVPTextureFormat::e format = TVPTextureFormat::None) {
    if(!software)
        return nullptr;
    return new tTVPBgfxTexture2D(software, pixel, pitch, format);
}

iTVPTexture2D *UnwrapBgfxTexture(iTVPTexture2D *texture) {
    auto *bgfxTexture = dynamic_cast<tTVPBgfxTexture2D *>(texture);
    if(bgfxTexture && bgfxTexture->GetSoftwareTexture())
        return bgfxTexture->GetSoftwareTexture();
    return texture;
}

tRenderTexRectArray UnwrapRectTextures(
    const tRenderTexRectArray &textures,
    std::vector<tRenderTexRectArray::Element> &storage) {
    storage.clear();
    storage.reserve(textures.size());
    for(size_t i = 0; i < textures.size(); ++i)
        storage.emplace_back(UnwrapBgfxTexture(textures[i].first), textures[i].second);
    return tRenderTexRectArray(storage.data(), storage.size());
}

tRenderTexQuadArray UnwrapQuadTextures(
    const tRenderTexQuadArray &textures,
    std::vector<tRenderTexQuadArray::Element> &storage) {
    storage.clear();
    storage.reserve(textures.size());
    for(size_t i = 0; i < textures.size(); ++i)
        storage.emplace_back(UnwrapBgfxTexture(textures[i].first), textures[i].second);
    return tRenderTexQuadArray(storage.data(), storage.size());
}

} // namespace

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
        return WrapBgfxTexture(Software->CreateTexture2D(pixel, pitch, w, h, format, flags),
                               pixel, pitch, format);
    }

    iTVPTexture2D *CreateTexture2D(tTVPBitmap *bmp) override {
        return WrapBgfxTexture(Software->CreateTexture2D(bmp));
    }

    iTVPTexture2D *CreateTexture2D(TJS::tTJSBinaryStream *stream) override {
        return WrapBgfxTexture(Software->CreateTexture2D(stream));
    }

    iTVPTexture2D *CreateTexture2D(unsigned int neww, unsigned int newh,
                                   iTVPTexture2D *tex) override {
        return WrapBgfxTexture(
            Software->CreateTexture2D(neww, newh, UnwrapBgfxTexture(tex)));
    }

    iTVPRenderMethod *GetRenderMethod(const char *name, uint32_t *hint = nullptr) override {
        return Software->GetRenderMethod(name, hint);
    }

    bool GetRenderStat(unsigned int &drawCount, uint64_t &vmemsize) override {
        return Software->GetRenderStat(drawCount, vmemsize);
    }

    bool GetTextureStat(iTVPTexture2D *texture, uint64_t &vmemsize) override {
        return Software->GetTextureStat(UnwrapBgfxTexture(texture), vmemsize);
    }

    void BeginStencil(iTVPTexture2D *reftex) override {
        Software->BeginStencil(UnwrapBgfxTexture(reftex));
    }
    void EndStencil() override { Software->EndStencil(); }
    void SetRenderTarget(iTVPTexture2D *target) override {
        Software->SetRenderTarget(UnwrapBgfxTexture(target));
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
        auto unwrappedTextures = UnwrapRectTextures(textures, unwrappedStorage);
        Software->OperateRect(method, UnwrapBgfxTexture(target),
                              UnwrapBgfxTexture(refTarget), targetRect,
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
        auto unwrappedTextures = UnwrapQuadTextures(textures, unwrappedStorage);
        Software->OperateTriangles(method, nTriangles, UnwrapBgfxTexture(target),
                                   UnwrapBgfxTexture(refTarget), clipRect,
                                   targetPoints, unwrappedTextures);
    }

    void OperatePerspective(iTVPRenderMethod *method, int nQuads,
                            iTVPTexture2D *target, iTVPTexture2D *refTarget,
                            const tTVPRect &clipRect, const tTVPPointD *targetPoints,
                            const tRenderTexQuadArray &textures) override {
        std::vector<tRenderTexQuadArray::Element> unwrappedStorage;
        auto unwrappedTextures = UnwrapQuadTextures(textures, unwrappedStorage);
        Software->OperatePerspective(method, nQuads, UnwrapBgfxTexture(target),
                                     UnwrapBgfxTexture(refTarget), clipRect,
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
