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
        // First-run engine prefs bootstrap. The native engine defaults
        // renderer=software, and we keep that as the Android default because
        // the OpenGL path still has compatibility render errors on some KRKR
        // titles. The setting remains user-selectable while the bgfx renderer
        // migration is staged.
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
