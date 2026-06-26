package org.github.krkr2

import android.annotation.SuppressLint
import android.app.Application
import android.content.Context
import android.util.Log
import kotlin.system.exitProcess

class KR2Application : Application() {
    override fun onCreate() {
        super.onCreate()
        context = applicationContext
        installUncaughtExceptionLogger()
        runCatching {
            val logFile = LauncherPrefs.beginUnifiedLogSession(this)
            LauncherPrefs.writeLauncherLog(this, "KR2Application.onCreate pid=${android.os.Process.myPid()} logFile=$logFile")
        }.onFailure {
            Log.w("KR2Application", "early log session failed", it)
        }
        // First-run and migration bootstrap for engine prefs. Renderer values
        // are normalized every process start because old builds and in-game
        // preference files may still contain unsupported native Vulkan values.
        runCatching {
            val prefs = getSharedPreferences("krkr2_launcher_bootstrap", MODE_PRIVATE)
            val snap = KrkrPrefsStore.load(this)
            val seed = mutableMapOf<String, String>()
            val renderer = snap.items["renderer"]
            val normalizedRenderer = LauncherPrefs.normalizeRendererPreference(renderer)
            if ((renderer == "vulkan" || renderer == "vk") &&
                "graphics_backend" !in snap.items) {
                seed["graphics_backend"] = "vulkan"
            }
            if (renderer != normalizedRenderer) seed["renderer"] = normalizedRenderer
            val normalizedGraphicsBackend =
                LauncherPrefs.normalizeGraphicsBackendPreference(snap.items["graphics_backend"])
            if (snap.items["graphics_backend"] != normalizedGraphicsBackend) {
                seed["graphics_backend"] = seed["graphics_backend"] ?: normalizedGraphicsBackend
            }
            if (!prefs.getBoolean("engine_defaults_v4_applied", false)) {
                if ("graphics_backend" !in snap.items) seed["graphics_backend"] = "opengl"
                if ("ogl_accurate_render" !in snap.items) seed["ogl_accurate_render"] = "0"
                if (LauncherPrefs.ENGINE_KEY_FFMPEG_IMAGE_DECODER !in snap.items) {
                    seed[LauncherPrefs.ENGINE_KEY_FFMPEG_IMAGE_DECODER] = "0"
                }
                if (LauncherPrefs.ENGINE_KEY_FFMPEG_DECODE_MODE !in snap.items) {
                    seed[LauncherPrefs.ENGINE_KEY_FFMPEG_DECODE_MODE] = LauncherPrefs.FFMPEG_DECODE_MODE_SOFTWARE
                }
                prefs.edit()
                    .putBoolean("engine_defaults_v1_applied", true)
                    .putBoolean("engine_defaults_v2_applied", true)
                    .putBoolean("engine_defaults_v3_applied", true)
                    .putBoolean("engine_defaults_v4_applied", true)
                    .apply()
            }
            if (seed.isNotEmpty()) KrkrPrefsStore.update(this, seed)
        }
    }

    private fun installUncaughtExceptionLogger() {
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            runCatching {
                LauncherPrefs.writeLauncherLog(
                    this,
                    "UNCAUGHT Java exception thread=${thread.name} id=${thread.id}",
                    throwable,
                )
            }.onFailure {
                Log.e("KR2Application", "failed to write uncaught exception log", it)
            }
            if (previous != null) {
                previous.uncaughtException(thread, throwable)
            } else {
                Log.e("KR2Application", "uncaught exception", throwable)
                android.os.Process.killProcess(android.os.Process.myPid())
                exitProcess(10)
            }
        }
    }

    companion object {
        @SuppressLint("StaticFieldLeak")
        @JvmStatic
        lateinit var context: Context
    }
}
