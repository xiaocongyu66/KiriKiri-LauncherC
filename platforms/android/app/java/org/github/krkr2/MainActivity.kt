package org.github.krkr2

import android.app.Activity
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.WindowManager
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.libsdl.app.SDLAudioManager
import org.libsdl.app.SDLActivity
import org.tvp.kirikiri2.KR2Activity

class MainActivity : KR2Activity() {

    companion object {
        const val EXTRA_GAME_DIR = "extra_game_dir"
        const val EXTRA_GAME_TITLE = "extra_game_title"
        const val EXTRA_LAUNCH_FILE = "extra_launch_file"
    }

    private var sessionStartedAt = 0L
    private var launchRecorded = false
    private var orientationListener: android.view.OrientationEventListener? = null

    private fun launchExtra(name: String): String = intent?.getStringExtra(name).orEmpty()

    private fun logLifecycle(message: String, throwable: Throwable? = null) {
        val detail =
            "gameDir=${launchExtra(EXTRA_GAME_DIR)} launchFile=${launchExtra(EXTRA_LAUNCH_FILE)} thread=${Thread.currentThread().name}"
        LauncherPrefs.writeLauncherLog(
            this,
            "MainActivity.$message $detail",
            throwable,
        )
        writeNativeLifecycleLog("MainActivity.$message", detail)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        logLifecycle("onCreate#enter saved=${savedInstanceState != null}")
        AngleDriverController.configureBeforeGl(this)
        super.setEnableVirtualButton(false)
        logLifecycle("onCreate#before-super")
        super.onCreate(savedInstanceState)
        logLifecycle("onCreate#after-super taskRoot=$isTaskRoot")

        if (!isTaskRoot) {
            logLifecycle("onCreate#not-task-root-return")
            return
        }

        val nativeLogFile = LauncherPrefs.configureNativeLogging(this)
        logLifecycle("onCreate#native-log-configured path=$nativeLogFile")
        logLifecycle("onCreate#sdl-java version=${SDLActivity.getCompiledVersionString()} libs=${SDLActivity.getDefaultLibrariesString()}")
        val useFfmpegImageDecoder = LauncherPrefs.getUseFfmpegImageDecoder(this)
        val ffmpegDecodeMode = LauncherPrefs.getFfmpegDecodeMode(this)
        KR2Activity.setUseFFmpegImageDecoder(useFfmpegImageDecoder)
        KR2Activity.setFFmpegDecodeMode(LauncherPrefs.getFfmpegDecodeModeCode(this))
        logLifecycle("onCreate#ffmpeg ffmpegImageDecoder=$useFfmpegImageDecoder ffmpegDecodeMode=$ffmpegDecodeMode")

        val lp = window.attributes
        lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        window.attributes = lp
        logLifecycle("onCreate#cutout-configured")

        // Force-landscape goes through the helper instead of a one-shot
        // requestedOrientation assignment. The helper additionally pins the
        // system auto-rotate flag (when WRITE_SETTINGS is granted) so OEM
        // rotation locks cannot override us.
        ForceLandscapeHelper.apply(this, true)
        orientationListener = ForceLandscapeHelper.stickyListener(this)
        logLifecycle("onCreate#landscape-applied requestedOrientation=$requestedOrientation")

        if (!checkStoragePermission()) {
            logLifecycle("onCreate#request-storage-permission")
            requestStoragePermission()
        }

        LauncherPrefs.writeLauncherLog(
            this,
            "MainActivity.onCreate gameDir=${intent?.getStringExtra(EXTRA_GAME_DIR).orEmpty()} launchFile=${intent?.getStringExtra(EXTRA_LAUNCH_FILE).orEmpty()} taskRoot=$isTaskRoot ffmpegImageDecoder=$useFfmpegImageDecoder ffmpegDecodeMode=$ffmpegDecodeMode nativeLogFile=$nativeLogFile"
        )

        logLifecycle("onCreate#sdl-audio-init-start")
        SDLAudioManager.nativeSetupJNI()
        SDLAudioManager.initialize()
        SDLAudioManager.setContext(getContext())
        logLifecycle("onCreate#sdl-audio-init-done")
    }

    override fun onStart() {
        super.onStart()
        logLifecycle("onStart")
        val gameDir = intent?.getStringExtra(EXTRA_GAME_DIR)
        if (!gameDir.isNullOrEmpty()) {
            LauncherPrefs.writeLauncherLog(this, "MainActivity.onStart gameDir=$gameDir launchFile=${intent?.getStringExtra(EXTRA_LAUNCH_FILE).orEmpty()}")
            LauncherPrefs.setLastGamePath(this, gameDir)
            if (!launchRecorded) {
                LauncherPrefs.recordLaunch(this, gameDir)
                launchRecorded = true
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        logLifecycle("onNewIntent#before-super newGameDir=${intent.getStringExtra(EXTRA_GAME_DIR).orEmpty()} newLaunchFile=${intent.getStringExtra(EXTRA_LAUNCH_FILE).orEmpty()}")
        super.onNewIntent(intent)
        setIntent(intent)
        logLifecycle("onNewIntent#after-setIntent")
    }

    override fun onResume() {
        super.onResume()
        sessionStartedAt = System.currentTimeMillis()
        logLifecycle("onResume sessionStartedAt=$sessionStartedAt")
        // Re-assert landscape on every resume; some OEMs reset
        // requestedOrientation when an activity is brought back from the
        // background (e.g. after a notification consumes input focus).
        ForceLandscapeHelper.apply(this, true)
        orientationListener?.let { if (it.canDetectOrientation()) it.enable() }
    }

    override fun onPause() {
        logLifecycle("onPause#enter sessionStartedAt=$sessionStartedAt")
        recordSessionTime()
        orientationListener?.disable()
        super.onPause()
        logLifecycle("onPause#after-super")
    }

    override fun onDestroy() {
        logLifecycle("onDestroy#enter sessionStartedAt=$sessionStartedAt")
        recordSessionTime()
        orientationListener?.disable()
        orientationListener = null
        // Restore the user's auto-rotate setting before the activity is gone.
        ForceLandscapeHelper.release(this)
        // Do NOT call SDLAudioManager.release(this) here. Cocos2dxActivity ->
        // SDLActivity.onDestroy() already calls it once (libsdl/SDLActivity
        // line 591). Calling release twice in a row triggers a double
        // unregisterAudioDeviceCallback, which on Android 13+ destroys an
        // internal SDL audio mutex while the GLThread is still draining a
        // final frame and ends in:
        //   FORTIFY: pthread_mutex_lock called on a destroyed mutex
        //   Fatal signal 6 (SIGABRT) in tid X (GLThread Y)
        // observed in 78.log PID 14643 / GLThread 105.
        super.onDestroy()
        logLifecycle("onDestroy#after-super")
    }

    private fun recordSessionTime() {
        val gameDir = intent?.getStringExtra(EXTRA_GAME_DIR) ?: return
        if (sessionStartedAt > 0L) {
            val delta = System.currentTimeMillis() - sessionStartedAt
            if (delta > 0L) {
                LauncherPrefs.recordPlayTime(this, gameDir, delta)
                LauncherPrefs.writeLauncherLog(this, "MainActivity.recordSessionTime gameDir=$gameDir deltaMs=$delta")
            }
            sessionStartedAt = 0L
        }
    }

    private fun checkStoragePermission(): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager()
        }
        return ContextCompat.checkSelfPermission(
            this, android.Manifest.permission.WRITE_EXTERNAL_STORAGE
        ) == PackageManager.PERMISSION_GRANTED
    }

    private fun requestStoragePermission(): Boolean {
        var granted = false

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            registerForActivityResult(
                ActivityResultContracts.RequestPermission()
            ) { result -> granted = result }
                .launch(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
            return granted
        }

        val startForResult = registerForActivityResult(
            ActivityResultContracts.StartActivityForResult()
        ) { result ->
            granted = result.resultCode == Activity.RESULT_OK && checkStoragePermission()
        }

        MaterialAlertDialogBuilder(this)
            .setTitle(getString(org.github.krkr2.R.string.request_storage_permission_title))
            .setMessage(getString(org.github.krkr2.R.string.request_storage_permission))
            .setPositiveButton(getString(org.github.krkr2.R.string.ok)) { _, _ ->
                startForResult.launch(
                    Intent(
                        Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        android.net.Uri.fromParts("package", packageName, null)
                    )
                )
            }
            .setNegativeButton(getString(org.github.krkr2.R.string.cancel), null)
            .show()

        return granted
    }
}
