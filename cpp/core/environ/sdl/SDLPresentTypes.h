#pragma once

enum class TVPSDLPresentPath {
    None,
    AndroidEGL,
    AndroidFlutterDirect,
    SDLWindowSurface,
};

struct TVPSDLPresentRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool IsEmpty() const { return w <= 0 || h <= 0; }

    bool IsFullFrame(int width, int height) const {
        return x == 0 && y == 0 && w == width && h == height;
    }
};

struct TVPSDLTexturePresentPlan {
    int textureWidth = 0;
    int textureHeight = 0;
    TVPSDLPresentRect dirtyRect;
    TVPSDLPresentRect fallbackRect;
    bool forceFullFrame = false;
    bool directPartialAllowed = false;
};

struct TVPSDLTexturePresentResult {
    TVPSDLPresentPath path = TVPSDLPresentPath::None;
    TVPSDLPresentRect sourceRect;
    bool fullFrame = false;
    bool nativeGL = false;
    bool cpuCopyFree = false;

    bool Presented() const { return path != TVPSDLPresentPath::None; }
};
