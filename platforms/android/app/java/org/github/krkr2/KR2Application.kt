package org.github.krkr2;

import android.annotation.SuppressLint
import android.app.Application
import android.content.Context

class KR2Application : Application() {
    override fun onCreate() {
        super.onCreate()
        context = applicationContext
        // First-run engine prefs bootstrap. The native engine defaults
        // renderer=software, and we keep that as the Android default because
        // the OpenGL path still has compatibility render errors on some KRKR
        // titles. The setting remains user-selectable for future ANGLE/VK work.
        runCatching {
            val prefs = getSharedPreferences("krkr2_launcher_bootstrap", MODE_PRIVATE)
            if (!prefs.getBoolean("engine_defaults_v2_applied", false)) {
                val snap = KrkrPrefsStore.load(this)
                // Seed missing values and migrate the previous OpenGL bootstrap
                // default back to software once.
                val seed = mutableMapOf<String, String>()
                if (snap.items["renderer"] != "software") seed["renderer"] = "software"
                if ("ogl_accurate_render" !in snap.items) seed["ogl_accurate_render"] = "0"
                if (LauncherPrefs.ENGINE_KEY_FFMPEG_IMAGE_DECODER !in snap.items) {
                    seed[LauncherPrefs.ENGINE_KEY_FFMPEG_IMAGE_DECODER] = "0"
                }
                if (LauncherPrefs.ENGINE_KEY_FFMPEG_DECODE_MODE !in snap.items) {
                    seed[LauncherPrefs.ENGINE_KEY_FFMPEG_DECODE_MODE] = LauncherPrefs.FFMPEG_DECODE_MODE_SOFTWARE
                }
                if (seed.isNotEmpty()) KrkrPrefsStore.update(this, seed)
                prefs.edit()
                    .putBoolean("engine_defaults_v1_applied", true)
                    .putBoolean("engine_defaults_v2_applied", true)
                    .apply()
            }
        }
    }

    companion object {
        @SuppressLint("StaticFieldLeak")
        @JvmStatic
        lateinit var context: Context
    }
}
