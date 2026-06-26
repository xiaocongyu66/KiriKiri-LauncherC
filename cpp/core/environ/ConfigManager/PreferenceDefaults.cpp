#include "PreferenceDefaults.h"

#include "GlobalConfigManager.h"

namespace {

void EnsureBool(iSysConfigManager *config, const char *key, bool value) {
    config->GetValue<bool>(key, value);
}

void EnsureString(iSysConfigManager *config, const char *key,
                  const char *value) {
    config->GetValue<std::string>(key, value);
}

void EnsureFloat(iSysConfigManager *config, const char *key, float value) {
    config->GetValue<float>(key, value);
}

} // namespace

void TVPInitializePreferenceDefaults() {
    static bool initialized = false;
    if(initialized)
        return;
    initialized = true;

    auto *config = GlobalConfigManager::GetInstance();
    EnsureBool(config, "outputlog", true);
    EnsureBool(config, "showfps", false);
    EnsureString(config, "fps_limit", "60");
    EnsureString(config, "renderer", "software");
    EnsureString(config, "graphics_backend", "opengl");
    EnsureString(config, "default_font", "");
    EnsureBool(config, "force_default_font", false);
#if defined(CC_TARGET_OS_IPHONE)
    EnsureString(config, "memusage", "high");
#else
    EnsureString(config, "memusage", "unlimited");
#endif
    EnsureBool(config, "keep_screen_alive", true);
    EnsureFloat(config, "vcursor_scale", 0.5f);
    EnsureFloat(config, "menu_handler_opa", 0.15f);
    EnsureBool(config, "remember_last_path", true);
#if defined(__ANDROID__) || defined(ANDROID) ||                                 \
    (defined(CC_TARGET_PLATFORM) && defined(CC_PLATFORM_ANDROID) &&             \
     CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
    EnsureBool(config, "hide_android_sys_btn", false);
    EnsureBool(config, "ffmpeg_image_decoder", false);
    EnsureString(config, "ffmpeg_decode_mode", "software");
#endif

    EnsureString(config, "software_draw_thread", "0");
    EnsureString(config, "software_compress_tex", "none");

#if defined(CC_TARGET_OS_IPHONE)
    EnsureBool(config, "GL_EXT_shader_framebuffer_fetch", true);
#else
    EnsureBool(config, "GL_EXT_shader_framebuffer_fetch", false);
#endif
    EnsureBool(config, "GL_ARM_shader_framebuffer_fetch", true);
    EnsureBool(config, "GL_NV_shader_framebuffer_fetch", true);
    EnsureBool(config, "GL_EXT_copy_image", false);
    EnsureBool(config, "GL_OES_copy_image", false);
    EnsureBool(config, "GL_ARB_copy_image", false);
    EnsureBool(config, "GL_NV_copy_image", false);
    EnsureBool(config, "GL_EXT_clear_texture", true);
    EnsureBool(config, "GL_ARB_clear_texture", true);
    EnsureBool(config, "GL_QCOM_alpha_test", true);
    EnsureBool(config, "ogl_accurate_render", false);
    EnsureString(config, "ogl_max_texsize", "0");
    EnsureString(config, "ogl_compress_tex", "none");
}
