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
        if(pixel)
            Upload(pixel, pitch, format);
        else
            UploadFromSoftware();
    }

    ~tTVPBgfxTexture2D() override {
        if(BgfxHandle != InvalidBgfxTextureHandle)
            TVPBgfx::DestroyTexture2D(BgfxHandle);
        if(Software)
            Software->Release();
    }

    void SetSize(unsigned int w, unsigned int h) override {
        iTVPTexture2D::SetSize(w, h);
        if(Software)
            Software->SetSize(w, h);
        if(BgfxHandle != InvalidBgfxTextureHandle) {
            TVPBgfx::DestroyTexture2D(BgfxHandle);
            BgfxHandle = InvalidBgfxTextureHandle;
        }
    }

    TVPTextureFormat::e GetFormat() const override { return Software->GetFormat(); }
    const void *GetScanLineForRead(tjs_uint line) override { return Software->GetScanLineForRead(line); }
    const void *GetPixelData() override { return Software->GetPixelData(); }
    TVPTextureFormat::e GetPixelDataFormat() const override { return Software->GetPixelDataFormat(); }
    void *GetScanLineForWrite(tjs_uint line) override { return Software->GetScanLineForWrite(line); }
    tjs_int GetPitch() const override { return Software->GetPitch(); }
    uint32_t GetPoint(int x, int y) override { return Software->GetPoint(x, y); }
    void SetPoint(int x, int y, uint32_t color) override {
        Software->SetPoint(x, y, color);
        UploadFromSoftware();
    }
    bool IsStatic() override { return Software->IsStatic(); }
    bool IsOpaque() override { return Software->IsOpaque(); }
    cocos2d::Texture2D *GetAdapterTexture(cocos2d::Texture2D *origTex) override {
        return Software->GetAdapterTexture(origTex);
    }
    bool GetScale(float &x, float &y) override { return Software->GetScale(x, y); }
    unsigned int GetNativeGLTextureId() const override { return Software->GetNativeGLTextureId(); }
    void InvalidatePixelCache() override { Software->InvalidatePixelCache(); }

    void Update(const void *pixel, TVPTextureFormat::e format, int pitch,
                const tTVPRect &rect) override {
        Software->Update(pixel, format, pitch, rect);
        UploadFromSoftware();
    }

private:
    void Upload(const void *pixel, int pitch, TVPTextureFormat::e format) {
        if(!pixel || pitch <= 0)
            return;
        if(BgfxHandle == InvalidBgfxTextureHandle) {
            BgfxHandle = TVPBgfx::CreateTexture2D(GetWidth(), GetHeight(), pixel,
                                                  pitch, static_cast<int>(format));
        } else {
            TVPBgfx::UpdateTexture2D(BgfxHandle, GetWidth(), GetHeight(), pixel,
                                     pitch, static_cast<int>(format));
        }
    }

    void UploadFromSoftware() {
        if(!Software)
            return;
        Upload(Software->GetPixelData(), Software->GetPitch(),
               Software->GetPixelDataFormat());
    }

    iTVPTexture2D *Software = nullptr;
    uint16_t BgfxHandle = InvalidBgfxTextureHandle;
};

iTVPTexture2D *WrapBgfxTexture(iTVPTexture2D *software, const void *pixel = nullptr,
                               int pitch = 0,
                               TVPTextureFormat::e format = TVPTextureFormat::None) {
    return new tTVPBgfxTexture2D(software, pixel, pitch, format);
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
        return WrapBgfxTexture(Software->CreateTexture2D(neww, newh, tex));
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
        const char *methodName = method ? method->GetName().c_str() : "";
        TVPBgfx::StageRectBatch(methodName, targetRect.left,
                                targetRect.top, targetRect.get_width(),
                                targetRect.get_height(),
                                static_cast<uint32_t>(textures.size()));
        Software->OperateRect(method, target, refTarget, targetRect, textures);
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

void TVPRegisterBgfxVulkanRenderManager() {
    TVPRegisterRenderManager("vulkan", TVPCreateBgfxVulkanRenderManager);
    TVPRegisterRenderManager("bgfx-vk", TVPCreateBgfxVulkanRenderManager);
}
