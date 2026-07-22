# 2026-07-22 千恋＊万花 特殊 CG 只剩脸 — WEBP 加载修复

## 范围

- **游戏：千恋＊万花**（不是游戏 78）
- 症状：特殊事件 CG 人物只剩脸/头发，身体缺失
- 锚点资源（log）：`ev301a.pimg`、`ev701a.pimg`
  - 全屏/大底图层：RIFF/WEBP（`header=0x46464952`）
  - 脸差分：小尺寸 WEBP 或全屏 sparse WEBP

## 根因

1. **`TVPLoadWEBP` 扫描线协议错误**（LauncherC 旧实现）
   - 只 `scanlinecallback(0)` 一次，把整图写进第 0 行指针
   - 只 `scanlinecallback(-1)` 一次 → 只对第 0 行做 color-key/tiling 提交
   - PNG/JPEG/Aether 均为 **逐行 get + 逐行 commit**
   - FFmpeg 图解码失败回退到 `TVPLoadWEBP` 时，底图身体层会坏；脸差分有时仍可见

2. **FFmpeg 图路径 `HasAlpha` 过严**
   - 颜色帧一律 swscale 到 RGBA，但 `HasAlpha` 只看源 pixel format flag
   - 源无 alpha flag 时 `gpfRGB` → `IsOpaque=true`，透明 CG 层合成异常

3. **WEBP 四CC 检测过严**
   - 旧代码要求 `WEBPVP8`；应只要求 `WEBP`（VP8/VP8L/VP8X 均可）

## 修改

- `cpp/core/visual/LoadWEBP.cpp`：对齐 Aether — 解码到临时缓冲，按行 `scanlinecallback(y)` + `-1`
- `cpp/core/visual/GraphicsLoaderIntf.cpp`：颜色帧 `HasAlpha = !grayscale`；router 用 `WEBP` 四CC
- `cpp/plugins/psbfile/PSBMedia.cpp`：`IsSupportedImageHeader` 同步 `WEBP` 检测

## 验证

- 千恋打开 `ev301a` / `ev701a`：身体底图 + 脸差分应同时可见
- 日志仍可出现早期 `[missing] EmotePlayer`（插件未加载前探测），可忽略
- 软件/OpenGL 两路径各截一张对照

## 非目标

- 游戏 78 的 `ev401a` 不在本修复验收范围内（另一作品）
