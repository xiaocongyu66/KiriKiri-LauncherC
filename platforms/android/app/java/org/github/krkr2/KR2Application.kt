package org.github.krkr2;

import android.annotation.SuppressLint
import android.app.ActivityManager
import android.app.Application
import android.content.Context
import android.content.Intent
import android.os.Build
import kotlin.system.exitProcess

class KR2Application : Application() {
    override fun onCreate() {
        super.onCreate()
        context = applicationContext
        installCrashReturnHandler()
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

    private fun installCrashReturnHandler() {
        val processName = currentProcessName()
        val isGameProcess = processName.endsWith(":game")
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            runCatching {
                LauncherPrefs.writeLauncherLog(
                    this,
                    "Uncaught exception in process=$processName thread=${thread.name}",
                    throwable
                )
            }
            if (isGameProcess) {
                runCatching {
                    val intent = Intent(this, LauncherActivity::class.java)
                    intent.addFlags(
                        Intent.FLAG_ACTIVITY_NEW_TASK or
                            Intent.FLAG_ACTIVITY_CLEAR_TOP or
                            Intent.FLAG_ACTIVITY_SINGLE_TOP
                    )
                    intent.putExtra("krkr2_game_crash_return", true)
                    startActivity(intent)
                }
            }
            if (previous != null) {
                previous.uncaughtException(thread, throwable)
            } else {
                exitProcess(10)
            }
        }
    }

    private fun currentProcessName(): String {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            return Application.getProcessName()
        }
        val pid = android.os.Process.myPid()
        val manager = getSystemService(ACTIVITY_SERVICE) as? ActivityManager
        val name = manager?.runningAppProcesses
            ?.firstOrNull { it.pid == pid }
            ?.processName
        return name ?: packageName
    }

    companion object {
        @SuppressLint("StaticFieldLeak")
        @JvmStatic
        lateinit var context: Context
    }
}
