package org.github.krkr2

/**
 * Schema mirror of `cpp/core/environ/ui/PreferenceConfig.h::initAllConfig()`.
 *
 * The C++ side ships its own renderer-config UI (TVPGlobalPreferenceForm)
 * driven by [tPreferenceScreen]. We can't easily reuse that on the launcher
 * side because the engine isn't running yet when the launcher is open, so
 * we mirror the schema in Kotlin and persist values straight into the same
 * `GlobalPreference.xml` the engine reads at startup. Single source of
 * truth: the XML file. UI is purely a view onto it.
 *
 * Update rule: when [PreferenceConfig.h] grows new keys, mirror them here.
 * Keys MUST match exactly — any drift means the engine reads a different
 * value than the launcher displays.
 */
object KrkrPrefsSchema {

    /**
     * One configurable knob. All branches share key/captionRes; types differ.
     */
    sealed interface PrefItem {
        val key: String
        val captionRes: String  // resource key name (we resolve via LauncherStrings extension)

        data class Bool(
            override val key: String,
            override val captionRes: String,
            val default: Boolean,
        ) : PrefItem

        data class Select(
            override val key: String,
            override val captionRes: String,
            val default: String,
            /** pair = (display caption resource, raw value persisted to XML) */
            val options: List<Pair<String, String>>,
        ) : PrefItem

        data class SliderFloat(
            override val key: String,
            override val captionRes: String,
            val default: Float,
            val min: Float = 0f,
            val max: Float = 1f,
        ) : PrefItem

        data class TextField(
            override val key: String,
            override val captionRes: String,
            val default: String,
        ) : PrefItem

        data class Constant(
            override val key: String,
            override val captionRes: String,
        ) : PrefItem
    }

    /**
     * One UI screen / collapsible section. Sections render as cards on a
     * single scrollable activity (we drop the engine's pop-form pattern;
     * no need for it on a single tall column).
     */
    data class Section(
        val titleRes: String,
        val items: List<PrefItem>,
    )

    // ---- Root preferences ---------------------------------------------------
    // Mirrors RootPreference.Preferences in PreferenceConfig.h:299-353.
    val ROOT = Section(
        titleRes = "preference_title",
        items = listOf(
            PrefItem.Bool("outputlog", "preference_output_log", true),
            PrefItem.Bool("showfps", "preference_show_fps", false),
            PrefItem.Select(
                key = "fps_limit",
                captionRes = "preference_fps_limit",
                default = "60",
                options = listOf("60" to "60", "45" to "45", "30" to "30", "15" to "15"),
            ),
            PrefItem.Select(
                key = "renderer",
                captionRes = "preference_select_renderer",
                default = "software",
                options = listOf(
                    "preference_opengl" to "opengl",
                    "preference_angle" to "angle",
                    "preference_angle_vk" to "angle-vk",
                    "preference_vulkan" to "vulkan",
                    "preference_software" to "software",
                ),
            ),
            PrefItem.TextField("default_font", "preference_default_font", ""),
            PrefItem.Bool("force_default_font", "preference_force_def_font", false),
            PrefItem.Select(
                key = "memusage",
                captionRes = "preference_mem_limit",
                default = "unlimited",
                options = listOf(
                    "preference_mem_unlimited" to "unlimited",
                    "preference_mem_high" to "high",
                    "preference_mem_medium" to "medium",
                    "preference_mem_low" to "low",
                ),
            ),
            PrefItem.Bool("keep_screen_alive", "preference_keep_screen_alive", true),
            PrefItem.SliderFloat("vcursor_scale", "preference_virtual_cursor_scale", 0.5f),
            PrefItem.SliderFloat("menu_handler_opa", "preference_menu_handler_opacity", 0.15f),
            PrefItem.Bool("remember_last_path", "preference_remember_last_path", true),
            PrefItem.Bool("hide_android_sys_btn", "preference_hide_android_sys_btn", false),
            PrefItem.Bool("ffmpeg_image_decoder", "preference_ffmpeg_image_decoder", false),
            PrefItem.Select(
                key = "ffmpeg_decode_mode",
                captionRes = "preference_ffmpeg_decode_mode",
                default = "software",
                options = listOf(
                    "preference_ffmpeg_decode_software" to "software",
                    "preference_ffmpeg_decode_hardware" to "hardware",
                ),
            ),
        ),
    )

    // ---- OpenGL renderer ----------------------------------------------------
    // Mirrors OpenglOptPreference.Preferences in PreferenceConfig.h:378-447.
    // `extensions` is a sub-section in the engine's UI; we flatten because a
    // single screen with grouped cards reads better than a pop-form.
    val OPENGL = Section(
        titleRes = "preference_opengl_renderer_opt",
        items = listOf(
            PrefItem.Constant("__opengl_extension_header", "preference_opengl_extension_desc"),
            PrefItem.Bool("GL_EXT_shader_framebuffer_fetch", "GL_EXT_shader_framebuffer_fetch", false),
            PrefItem.Bool("GL_ARM_shader_framebuffer_fetch", "GL_ARM_shader_framebuffer_fetch", true),
            PrefItem.Bool("GL_NV_shader_framebuffer_fetch", "GL_NV_shader_framebuffer_fetch", true),
            PrefItem.Bool("GL_EXT_copy_image", "GL_EXT_copy_image", false),
            PrefItem.Bool("GL_OES_copy_image", "GL_OES_copy_image", false),
            PrefItem.Bool("GL_ARB_copy_image", "GL_ARB_copy_image", false),
            PrefItem.Bool("GL_NV_copy_image", "GL_NV_copy_image", false),
            PrefItem.Bool("GL_EXT_clear_texture", "GL_EXT_clear_texture", true),
            PrefItem.Bool("GL_ARB_clear_texture", "GL_ARB_clear_texture", true),
            PrefItem.Bool("GL_QCOM_alpha_test", "GL_QCOM_alpha_test", true),
            PrefItem.Bool("ogl_accurate_render", "preference_ogl_accurate_render", false),
            PrefItem.Select(
                key = "ogl_max_texsize",
                captionRes = "preference_ogl_max_texsize",
                default = "0",
                options = listOf(
                    "preference_ogl_texsize_auto" to "0",
                    "preference_ogl_texsize_1024" to "1024",
                    "preference_ogl_texsize_2048" to "2048",
                    "preference_ogl_texsize_4096" to "4096",
                    "preference_ogl_texsize_8192" to "8192",
                    "preference_ogl_texsize_16384" to "16384",
                ),
            ),
            PrefItem.Select(
                key = "ogl_compress_tex",
                captionRes = "preference_ogl_compress_tex",
                default = "none",
                options = listOf(
                    "preference_ogl_compress_tex_none" to "none",
                    "preference_ogl_compress_tex_half" to "half",
                    "ETC2" to "etc2",
                    "PVRTC" to "pvrtc",
                ),
            ),
        ),
    )

    // ---- Software renderer --------------------------------------------------
    // Mirrors SoftRendererOptPreference.Preferences in PreferenceConfig.h:355-375.
    val SOFTWARE = Section(
        titleRes = "preference_soft_renderer_opt",
        items = listOf(
            PrefItem.Select(
                key = "software_draw_thread",
                captionRes = "preference_multi_draw_thread",
                default = "0",
                options = listOf(
                    "preference_draw_thread_auto" to "0",
                    "preference_draw_thread_1" to "1",
                    "preference_draw_thread_2" to "2",
                    "preference_draw_thread_3" to "3",
                    "preference_draw_thread_4" to "4",
                    "preference_draw_thread_5" to "5",
                    "preference_draw_thread_6" to "6",
                    "preference_draw_thread_7" to "7",
                    "preference_draw_thread_8" to "8",
                ),
            ),
            PrefItem.Select(
                key = "software_compress_tex",
                captionRes = "preference_software_compress_tex",
                default = "none",
                options = listOf(
                    "preference_soft_compress_tex_none" to "none",
                    "preference_soft_compress_tex_halfline" to "halfline",
                    "lz4" to "lz4",
                    "lz4+TLG5" to "lz4+tlg5",
                ),
            ),
        ),
    )

    /** All sections in display order. */
    val ALL: List<Section> = listOf(ROOT, OPENGL, SOFTWARE)

    /** Flat lookup: key -> item. Useful when querying without iterating. */
    val ALL_BY_KEY: Map<String, PrefItem> =
        ALL.flatMap { it.items }.associateBy { it.key }
}
