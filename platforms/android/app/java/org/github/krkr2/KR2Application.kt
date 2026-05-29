package org.github.krkr2;

import android.annotation.SuppressLint
import android.app.Application
import android.content.Context

class KR2Application : Application() {
    override fun onCreate() {
        super.onCreate()
        context = applicationContext
        // First-run engine prefs bootstrap. The native engine defaults
        // renderer=software (history: software was less crash-prone in
        // very old builds), but on modern Android devices software
        // rendering produces a black screen because the SDL window-mgr
        // setup races OpenGL context attach. opengl is the right default;
        // we plant it now so the engine picks it up on first launch.
        runCatching {
            val prefs = getSharedPreferences("krkr2_launcher_bootstrap", MODE_PRIVATE)
            if (!prefs.getBoolean("engine_defaults_v1_applied", false)) {
                val snap = KrkrPrefsStore.load(this)
                // Only seed values the user hasn't explicitly set yet.
                val seed = mutableMapOf<String, String>()
                if ("renderer" !in snap.items) seed["renderer"] = "opengl"
                if ("ogl_accurate_render" !in snap.items) seed["ogl_accurate_render"] = "0"
                if (LauncherPrefs.ENGINE_KEY_FFMPEG_IMAGE_DECODER !in snap.items) {
                    seed[LauncherPrefs.ENGINE_KEY_FFMPEG_IMAGE_DECODER] = "0"
                }
                if (seed.isNotEmpty()) KrkrPrefsStore.update(this, seed)
                prefs.edit().putBoolean("engine_defaults_v1_applied", true).apply()
            }
        }
    }

    companion object {
        @SuppressLint("StaticFieldLeak")
        @JvmStatic
        lateinit var context: Context
    }
}
