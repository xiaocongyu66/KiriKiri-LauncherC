# 2026-07-22 wamsoft 上游跟进

## 背景

[wamsoft org](https://github.com/orgs/wamsoft/repositories?type=all) 2026-06~07 恢复活跃。
本地浅克隆在 `/root/kiriki-work/upstream/wamsoft-{movie-player,AlphaMovie,tp_stub}`。

## 高价值仓（已扫）

| 仓 | 最近 | 与 LauncherC |
|----|------|----------------|
| **tp_stub** | 07-21 CMake GLOBAL 插件目标；声音扩展接口 | 插件 ABI 对齐时对照 |
| **krkrz** | 07-20 master 汇总；XP3Archive 死锁修复 | cherry-pick 存储/归档类补丁 |
| **krkrz_web** | 07-20 默认 ogl / Asyncify / layerExVector | 参考 ogl 切换；Web 非主路径 |
| **krkrz_android** | 07-03 Elements、virtualpad、Play | Android 宿主 UX 参考 |
| **movie-player** | 06-30 Android custom stream Seek+Read 互斥 | 我们走 FFmpeg/KRMovie；可借鉴 stream 串行化 |
| **AlphaMovie** | 07-04 初版完整 .amv 解码器 | 本地 `alphamovie.cpp` 仍是 API 桩；待整文件移植 |

## 本轮已做

1. **千恋 WEBP 只剩脸**：`LoadWEBP` 逐行 scanline + FFmpeg `HasAlpha` + pimg WEBP 头检测  
2. **AlphaMovie**：注释对齐上游 manual；API 表面已含 `showNextImage` / FPS* / screen*  
3. **未整仓合并** krkrz / movie-player（结构分叉大，避免一次大合并）

## 下一步（按收益）

1. 移植 `wamsoft/AlphaMovie` → `cpp/plugins/alphamovie.cpp`（或独立源文件 + CMake）  
2. 对照 `movie-player` Android 互斥，检查我们 storage stream 是否多线程 Seek+Read  
3. 从 `krkrz` 摘 XP3Archive 死锁修复到 `StorageIntf` / XP3 路径  
4. `tp_stub` 声音扩展接口若脚本需要再补

## 验证

- 千恋 `ev301a` / `ev701a` 身体+脸  
- 若游戏调用 AlphaMovie，启动不崩；真播 .amv 需完成移植后再测  
