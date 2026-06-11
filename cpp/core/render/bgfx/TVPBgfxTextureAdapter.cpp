#include "TVPBgfxTextureAdapter.h"

#include "BgfxRuntime.h"
#include "DebugIntf.h"
#include "EventIntf.h"

#include <cstdint>

namespace TVPBgfxAdapter {
namespace {

constexpr uint16_t InvalidBgfxTextureHandle = UINT16_MAX;
uint32_t BgfxCompactDropCount = 0;

class tTVPBgfxTexture2D : public iTVPTexture2D,
                           public tTVPCompactEventCallbackIntf {
public:
    tTVPBgfxTexture2D(iTVPTexture2D *software, const void *pixel = nullptr,
                      int pitch = 0,
                      TVPTextureFormat::e format = TVPTextureFormat::None) :
        iTVPTexture2D(software ? software->GetWidth() : 0,
                      software ? software->GetHeight() : 0),
        Software(software) {
        if(pixel && pitch > 0) {
            BgfxHandle = TVPBgfx::CreateTexture2D(
                Width, Height, pixel, pitch, static_cast<int>(format));
            BgfxPixelsCurrent = BgfxHandle != InvalidBgfxTextureHandle;
            RegisterCompactHookIfNeeded();
        } else if(Software) {
            BgfxHandle = TVPBgfx::CreateEmptyTexture2D(
                Width, Height, static_cast<int>(Software->GetFormat()));
            RegisterCompactHookIfNeeded();
            SyncFullTextureFromSoftware();
        }
    }

    ~tTVPBgfxTexture2D() override {
        DropBgfxTexture(false);
        if(Software)
            Software->Release();
    }

    iTVPTexture2D *GetSoftwareTexture() const { return Software; }

    void SetSize(unsigned int w, unsigned int h) override {
        iTVPTexture2D::SetSize(w, h);
        if(Software)
            Software->SetSize(w, h);
        DropBgfxTexture(false);
        if(Software) {
            BgfxHandle = TVPBgfx::CreateEmptyTexture2D(
                w, h, static_cast<int>(Software->GetFormat()));
            BgfxPixelsCurrent = false;
            RegisterCompactHookIfNeeded();
        }
    }

    void OnCompact(tjs_int level) override {
        if(level >= TVP_COMPACT_LEVEL_DEACTIVATE &&
           BgfxHandle != InvalidBgfxTextureHandle) {
            DropBgfxTexture(true);
            ++BgfxCompactDropCount;
            if(BgfxCompactDropCount <= 8 ||
               (BgfxCompactDropCount % 512) == 0) {
                TVPAddLog(TJS_W("[renderer] bgfx compact dropped texture #") +
                          ttstr(static_cast<int>(BgfxCompactDropCount)));
            }
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
        BgfxPixelsCurrent = false;
        return Software ? Software->GetScanLineForWrite(line) : nullptr;
    }
    tjs_int GetPitch() const override {
        return Software ? Software->GetPitch() : 0;
    }
    uint32_t GetPoint(int x, int y) override {
        return Software ? Software->GetPoint(x, y) : 0;
    }
    void SetPoint(int x, int y, uint32_t color) override {
        if(Software) {
            Software->SetPoint(x, y, color);
            BgfxPixelsCurrent = false;
        }
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
        BgfxPixelsCurrent = false;
        if(Software)
            Software->InvalidatePixelCache();
    }

    void Update(const void *pixel, TVPTextureFormat::e format, int pitch,
                const tTVPRect &rect) override {
        if(Software)
            Software->Update(pixel, format, pitch, rect);
        if(BgfxHandle == InvalidBgfxTextureHandle) {
            BgfxHandle = TVPBgfx::CreateEmptyTexture2D(
                Width, Height, static_cast<int>(format));
            RegisterCompactHookIfNeeded();
        }
        if(BgfxHandle != InvalidBgfxTextureHandle) {
            TVPBgfx::UpdateTexture2DRect(
                BgfxHandle, Width, Height, rect.left, rect.top,
                rect.get_width(), rect.get_height(), pixel, pitch,
                static_cast<int>(format));
            BgfxPixelsCurrent = true;
        }
    }

private:
    void RegisterCompactHookIfNeeded() {
        if(BgfxHandle == InvalidBgfxTextureHandle || CompactHookRegistered)
            return;
        TVPAddCompactEventHook(this);
        CompactHookRegistered = true;
    }

    void DropBgfxTexture(bool keepHook) {
        if(BgfxHandle != InvalidBgfxTextureHandle) {
            TVPBgfx::DestroyTexture2D(BgfxHandle);
            BgfxHandle = InvalidBgfxTextureHandle;
        }
        BgfxPixelsCurrent = false;
        if(CompactHookRegistered && !keepHook) {
            TVPRemoveCompactEventHook(this);
            CompactHookRegistered = false;
        }
    }

    void SyncFullTextureFromSoftware() {
        if(!Software || BgfxHandle == InvalidBgfxTextureHandle ||
           BgfxPixelsCurrent)
            return;
        const void *pixel = Software->GetScanLineForRead(0);
        const int pitch = Software->GetPitch();
        if(!pixel || pitch <= 0)
            return;
        TVPBgfx::UpdateTexture2D(BgfxHandle, Width, Height, pixel, pitch,
                                 static_cast<int>(Software->GetPixelDataFormat()));
        BgfxPixelsCurrent = true;
    }

    iTVPTexture2D *Software = nullptr;
    uint16_t BgfxHandle = InvalidBgfxTextureHandle;
    bool BgfxPixelsCurrent = false;
    bool CompactHookRegistered = false;
};

} // namespace

iTVPTexture2D *WrapTexture(iTVPTexture2D *software, const void *pixel,
                           int pitch, TVPTextureFormat::e format) {
    if(!software)
        return nullptr;
    return new tTVPBgfxTexture2D(software, pixel, pitch, format);
}

iTVPTexture2D *UnwrapTexture(iTVPTexture2D *texture) {
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
    for(size_t index = 0; index < textures.size(); ++index)
        storage.emplace_back(UnwrapTexture(textures[index].first),
                             textures[index].second);
    return tRenderTexRectArray(storage.data(), storage.size());
}

tRenderTexQuadArray UnwrapQuadTextures(
    const tRenderTexQuadArray &textures,
    std::vector<tRenderTexQuadArray::Element> &storage) {
    storage.clear();
    storage.reserve(textures.size());
    for(size_t index = 0; index < textures.size(); ++index)
        storage.emplace_back(UnwrapTexture(textures[index].first),
                             textures[index].second);
    return tRenderTexQuadArray(storage.data(), storage.size());
}

} // namespace TVPBgfxAdapter
