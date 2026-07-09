# 2026-07-07 固定 1920x1080 与 GPU 图层合成修复记忆

## 硬性要求

- 游戏画面必须固定为 `1920x1080`。
- 不采用“按设备物理像素动态改变游戏 surface 尺寸”的方案。
- 设备屏幕、Flutter view、Android physical surface 可以不同，但传给游戏渲染链路、native surface、presenter、输入映射的游戏坐标基准必须保持 `1920x1080`。
- 后续参考 KrKr2-Next 或其他项目时，只可借鉴 full-frame present、dirty gate、pitch-aware upload、EGL/window lifecycle、vsync pump 等模式；不要把它们的动态 surface 尺寸策略搬进 LauncherC。

## 本轮根因判断

- 软件渲染视觉正确，说明 Kirikiri CPU 合成结果本身大体可信。
- OpenGL/Flutter external presenter 出现旧帧残留、左右/上下旧画面、特殊 CG 旁边残缺人物，除 present/swap 时序外，还指向 GPU 图层合成时临时目标没有确定的完整内容。
- AetherKiri 的 `LayerIntf.cpp::Draw_GPU` 在透明父层或 transition-with-children 路径里使用完整 layer 尺寸的临时 bitmap，并先用 `CopySelfForRect()` 初始化完整 layer，再绘制 children。
- LauncherC 旧逻辑只按当前 clipped `rect` 申请临时 bitmap，并且 `MainImage == nullptr` 时跳过初始化。透明父层、transition、children 组合时，这容易让临时纹理里出现未初始化或上一帧内容；Android EGL 全帧 present 后就会被看见。

## 已改代码

文件：

- `cpp/core/environ/sdl/SDLPresentTypes.h`
- `cpp/core/visual/LayerIntf.cpp`

改动：

- 给 `kTVPSDLFixedGameSurfaceWidth/Height` 增加 `static_assert`，后续误改成非 `1920x1080` 会直接编译失败。
- 在 `tTJSNI_BaseLayer::Draw_GPU()` 的 `Opacity < 255 || (InTransition && TransWithChildren)` 且有可见 children 路径中，临时 `UpdateBitmapForChild` 从 `rect.get_width()/rect.get_height()` 改为 `Rect.get_width()/Rect.get_height()`。
- 新增 `rectForChild(0, 0, Rect.get_width(), Rect.get_height())`，作为完整 layer 坐标系的 child 合成范围。
- 移除 `if(MainImage != nullptr)` 限制，始终调用 `CopySelfForRect(UpdateBitmapForChild, 0, 0, rectForChild)`。
- child intersection 从 clipped `rect` 改为完整 `rectForChild`。
- 移除 `rect.set_offsets(0, 0)`，保持 AetherKiri 的坐标语义：最终 `DrawCompleted(rctar, UpdateBitmapForChild, rect, DisplayType, Opacity)` 仍使用当前需要提交给父目标的 clipped rect。
- `CompleteForWindow()` 的 GPU path 对齐 AetherKiri：窗口最终提交不再依赖 dirty bound，直接 `InternalComplete2_GPU(0,0,Rect.w,Rect.h)` 全帧完成，然后清掉 update region。

## 为什么这样改

- `CopySelfForRect()` 本身已经处理 `MainImage == nullptr`，会按 `DisplayType` 填透明色或中性色；不需要热路径额外判断。
- 完整 layer 临时目标能保证 children 画入的坐标和最终 clip rect 对齐，避免 clipped temp + child 全局坐标之间错位。
- 这与 AetherKiri 的稳定实现保持一致，比继续在 presenter 侧增加防御更接近问题根源。

## 后续验证重点

- 新日志应继续确认 `1920x1080`：
  - Java: `GAME_SURFACE_WIDTH=1920`, `GAME_SURFACE_HEIGHT=1080`
  - C++: `kTVPSDLFixedGameSurfaceWidth=1920`, `kTVPSDLFixedGameSurfaceHeight=1080`
- OpenGL presenter 日志里应看到 producer 阶段 `queue-android-egl`，随后 frame-end pump 阶段 `swap-android-egl`；不要再在 `BasicDrawDevice::Show` 内 immediate swap。
- 截图重点看：
  - `Screenshot_2026-07-06-04-32-55...` 同类主界面是否还出现灰白/彩色错层。
  - `Screenshot_2026-07-07-03-32-24...` 同类特殊 CG 两侧是否还残留人物碎片。
  - 快速过文本时是否还出现旧画面条带。

## 后续迁移边界

- 继续去 Cocos，迁向 Flutter + SDL3。
- 但直到 SDL3 host 完整接管窗口、frame pump、input、IME、presenter 前，当前 Cocos host 下的修复必须保持 `1920x1080` 固定游戏 surface。
- 参考 AetherKiri/krkrsdl2/krkrsdl3 时优先搬完整链路，不要拆散 present owner、swap owner、dirty owner。
