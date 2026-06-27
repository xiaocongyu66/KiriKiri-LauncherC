package org.github.krkr2

/**
 * Localised captions for engine preference keys.
 *
 * The engine schema (PreferenceConfig.h) ships its own caption resource
 * keys (e.g. `preference_show_fps`, `preference_select_renderer`). The
 * native engine resolves them through LocaleConfigManager; the launcher
 * runs *before* the engine, so we mirror the strings here for the two
 * languages we ship.
 *
 * Lookup is best-effort: unknown keys fall back to a humanised version of
 * the raw key, so adding new engine knobs in C++ won't crash the launcher
 * — they just appear with their machine name until translated.
 */
object KrkrPrefsCaptions {

    private val EN: Map<String, String> = mapOf(
        // Section headers
        "preference_title" to "Root preferences",
        "preference_opengl_renderer_opt" to "OpenGL renderer",
        "preference_soft_renderer_opt" to "Software renderer",

        // Root section
        "preference_output_log" to "Output log",
        "preference_show_fps" to "Show FPS",
        "preference_fps_limit" to "FPS limit",
        "preference_select_renderer" to "Render pipeline",
        "preference_graphics_backend" to "Graphics backend",
        "preference_opengl" to "OpenGL",
        "preference_vulkan" to "Vulkan",
        "preference_gpuapi" to "SDL GPU API",
        "preference_software" to "Software",
        "preference_default_font" to "Default font",
        "preference_force_def_font" to "Force default font",
        "preference_mem_limit" to "Memory limit",
        "preference_mem_unlimited" to "Unlimited",
        "preference_mem_high" to "High",
        "preference_mem_medium" to "Medium",
        "preference_mem_low" to "Low",
        "preference_keep_screen_alive" to "Keep screen on",
        "preference_virtual_cursor_scale" to "Virtual cursor scale",
        "preference_menu_handler_opacity" to "Menu trigger opacity",
        "preference_remember_last_path" to "Remember last path",
        "preference_hide_android_sys_btn" to "Hide system buttons",
        "preference_ffmpeg_image_decoder" to "FFmpeg image decoder",
        "preference_ffmpeg_decode_mode" to "FFmpeg decode mode",
        "preference_ffmpeg_decode_software" to "Software",
        "preference_ffmpeg_decode_hardware" to "Hardware first",

        // Software renderer
        "preference_multi_draw_thread" to "Draw threads",
        "preference_draw_thread_auto" to "Auto",
        "preference_draw_thread_1" to "1 thread",
        "preference_draw_thread_2" to "2 threads",
        "preference_draw_thread_3" to "3 threads",
        "preference_draw_thread_4" to "4 threads",
        "preference_draw_thread_5" to "5 threads",
        "preference_draw_thread_6" to "6 threads",
        "preference_draw_thread_7" to "7 threads",
        "preference_draw_thread_8" to "8 threads",
        "preference_software_compress_tex" to "Texture compression",
        "preference_soft_compress_tex_none" to "None",
        "preference_soft_compress_tex_halfline" to "Half-line",

        // OpenGL renderer
        "preference_opengl_extension_desc" to "GPU extension overrides. Disable if your device misrenders.",
        "preference_ogl_accurate_render" to "Accurate rendering",
        "preference_ogl_max_texsize" to "Max texture size",
        "preference_ogl_texsize_auto" to "Auto",
        "preference_ogl_texsize_1024" to "1024",
        "preference_ogl_texsize_2048" to "2048",
        "preference_ogl_texsize_4096" to "4096",
        "preference_ogl_texsize_8192" to "8192",
        "preference_ogl_texsize_16384" to "16384",
        "preference_ogl_compress_tex" to "Texture compression",
        "preference_ogl_compress_tex_none" to "None",
        "preference_ogl_compress_tex_half" to "Half precision",
    )

    private val ZH: Map<String, String> = mapOf(
        // Section headers
        "preference_title" to "基础设置",
        "preference_opengl_renderer_opt" to "OpenGL 渲染器",
        "preference_soft_renderer_opt" to "软件渲染器",

        // Root section
        "preference_output_log" to "输出日志",
        "preference_show_fps" to "显示帧率",
        "preference_fps_limit" to "帧率上限",
        "preference_select_renderer" to "渲染管线",
        "preference_graphics_backend" to "图形后端",
        "preference_opengl" to "OpenGL",
        "preference_vulkan" to "Vulkan",
        "preference_gpuapi" to "SDL GPU API",
        "preference_software" to "软件渲染",
        "preference_default_font" to "默认字体",
        "preference_force_def_font" to "强制使用默认字体",
        "preference_mem_limit" to "内存上限",
        "preference_mem_unlimited" to "不限制",
        "preference_mem_high" to "高",
        "preference_mem_medium" to "中",
        "preference_mem_low" to "低",
        "preference_keep_screen_alive" to "保持屏幕常亮",
        "preference_virtual_cursor_scale" to "虚拟光标缩放",
        "preference_menu_handler_opacity" to "菜单触发器透明度",
        "preference_remember_last_path" to "记住上次路径",
        "preference_hide_android_sys_btn" to "隐藏系统按键",
        "preference_ffmpeg_image_decoder" to "FFmpeg 图片解码",
        "preference_ffmpeg_decode_mode" to "FFmpeg 解码模式",
        "preference_ffmpeg_decode_software" to "软解",
        "preference_ffmpeg_decode_hardware" to "硬解优先",

        // Software renderer
        "preference_multi_draw_thread" to "绘制线程",
        "preference_draw_thread_auto" to "自动",
        "preference_draw_thread_1" to "1 线程",
        "preference_draw_thread_2" to "2 线程",
        "preference_draw_thread_3" to "3 线程",
        "preference_draw_thread_4" to "4 线程",
        "preference_draw_thread_5" to "5 线程",
        "preference_draw_thread_6" to "6 线程",
        "preference_draw_thread_7" to "7 线程",
        "preference_draw_thread_8" to "8 线程",
        "preference_software_compress_tex" to "纹理压缩",
        "preference_soft_compress_tex_none" to "不压缩",
        "preference_soft_compress_tex_halfline" to "半行压缩",

        // OpenGL renderer
        "preference_opengl_extension_desc" to "GPU 扩展开关。如果出现渲染异常，可尝试关闭。",
        "preference_ogl_accurate_render" to "精确渲染",
        "preference_ogl_max_texsize" to "最大纹理尺寸",
        "preference_ogl_texsize_auto" to "自动",
        "preference_ogl_texsize_1024" to "1024",
        "preference_ogl_texsize_2048" to "2048",
        "preference_ogl_texsize_4096" to "4096",
        "preference_ogl_texsize_8192" to "8192",
        "preference_ogl_texsize_16384" to "16384",
        "preference_ogl_compress_tex" to "纹理压缩",
        "preference_ogl_compress_tex_none" to "不压缩",
        "preference_ogl_compress_tex_half" to "半精度",
    )

    /**
     * Resolve a caption resource key for the active language.
     * GL extension names (`GL_EXT_*`) are passed through unchanged because
     * they're already canonical and don't have meaningful translations.
     */
    fun resolve(lang: String, key: String): String {
        if (key.startsWith("GL_")) return key
        val table = if (lang == LauncherPrefs.LANG_ZH) ZH else EN
        table[key]?.let { return it }
        // Fallback: humanise the key.
        val trimmed = key.removePrefix("preference_").replace('_', ' ').trim()
        return if (trimmed.isEmpty()) key else trimmed.replaceFirstChar { it.uppercase() }
    }
}
