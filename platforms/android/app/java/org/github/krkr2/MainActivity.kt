package org.github.krkr2

import android.app.Activity
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.Gravity
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import android.widget.FrameLayout
import java.lang.ref.WeakReference
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.embedding.engine.dart.DartExecutor
import io.flutter.embedding.android.FlutterTextureView
import io.flutter.embedding.android.FlutterView
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugins.GeneratedPluginRegistrant
import org.libsdl.app.SDLAudioManager
import org.libsdl.app.SDLActivity
import org.tvp.kirikiri2.KR2Activity

class MainActivity : KR2Activity() {

    companion object {
        const val EXTRA_GAME_DIR = "extra_game_dir"
        const val EXTRA_GAME_TITLE = "extra_game_title"
        const val EXTRA_LAUNCH_FILE = "extra_launch_file"

        private var currentActivity = WeakReference<MainActivity>(null)

        @JvmStatic
        fun showFlutterGameMainMenu(): Boolean {
            val activity = currentActivity.get() ?: return false
            activity.runOnUiThread {
                activity.showFlutterOverlayMenu()
            }
            return true
        }
    }

    private var sessionStartedAt = 0L
    private var launchRecorded = false
    private var orientationListener: android.view.OrientationEventListener? = null
    private var overlayEngine: FlutterEngine? = null
    private var overlayView: FlutterView? = null
    private var overlayParams: FrameLayout.LayoutParams? = null
    private var overlayChannel: MethodChannel? = null
    private var bgfxSurfaceView: SurfaceView? = null

    private external fun nativeGetMainMenuJson(): String
    private external fun nativeActivateMenuItem(path: String): Int
    private external fun nativePerformOverlayAction(action: String): Int
    private external fun nativeSetBgfxSurface(surface: Surface?, width: Int, height: Int)

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
        currentActivity = WeakReference(this)
        logLifecycle("onCreate#enter saved=${savedInstanceState != null}")
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
        if (!intent?.getStringExtra(EXTRA_GAME_DIR).isNullOrBlank()) {
            installIndependentBgfxSurface()
            installFlutterGameOverlay()
        }
    }

    override fun onStart() {
        super.onStart()
        currentActivity = WeakReference(this)
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
        if (currentActivity.get() === this) {
            currentActivity.clear()
        }
        overlayView?.detachFromFlutterEngine()
        overlayEngine?.destroy()
        overlayView = null
        overlayEngine = null
        overlayChannel = null
        nativeSetBgfxSurface(null, 0, 0)
        bgfxSurfaceView?.let { view ->
            runCatching { mFrameLayout?.removeView(view) }
        }
        bgfxSurfaceView = null
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

    private fun installIndependentBgfxSurface() {
        if (bgfxSurfaceView != null || mFrameLayout == null) return
        val surfaceView = SurfaceView(this)
        surfaceView.setZOrderMediaOverlay(false)
        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                nativeSetBgfxSurface(holder.surface, surfaceView.width.coerceAtLeast(1), surfaceView.height.coerceAtLeast(1))
            }

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                nativeSetBgfxSurface(holder.surface, width.coerceAtLeast(1), height.coerceAtLeast(1))
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                nativeSetBgfxSurface(null, 0, 0)
            }
        })
        val size = dp(2).coerceAtLeast(1)
        val params = FrameLayout.LayoutParams(size, size, Gravity.LEFT or Gravity.TOP)
        params.leftMargin = 0
        params.topMargin = 0
        bgfxSurfaceView = surfaceView
        mFrameLayout.addView(surfaceView, 0, params)
        LauncherPrefs.writeLauncherLog(this, "MainActivity.bgfx independent SurfaceView installed size=$size")
    }

    private fun installFlutterGameOverlay() {
        if (overlayView != null || mFrameLayout == null) return
        val engine = FlutterEngine(this)
        engine.navigationChannel.setInitialRoute("/game-overlay")
        GeneratedPluginRegistrant.registerWith(engine)
        val channel = MethodChannel(engine.dartExecutor.binaryMessenger, "org.github.krkr2/game_overlay")
        channel.setMethodCallHandler { call, result ->
            when (call.method) {
                "move" -> {
                    moveOverlay((call.argument<Double>("dx") ?: 0.0).toFloat(), (call.argument<Double>("dy") ?: 0.0).toFloat())
                    result.success(null)
                }
                "setExpanded" -> {
                    val expanded = call.argument<Boolean>("expanded") ?: false
                    val menuMode = call.argument<Boolean>("menuMode") ?: false
                    resizeOverlay(expanded, menuMode)
                    result.success(null)
                }
                "getMainMenu" -> result.success(runCatching { nativeGetMainMenuJson() }.getOrElse {
                    LauncherPrefs.writeLauncherLog(this, "MainActivity.overlay getMainMenu failed", it)
                    "[]"
                })
                "activateMenuItem" -> result.success(runCatching {
                    nativeActivateMenuItem(call.argument<String>("path").orEmpty())
                }.getOrElse {
                    LauncherPrefs.writeLauncherLog(this, "MainActivity.overlay activateMenuItem failed", it)
                    -100
                })
                "performOverlayAction" -> result.success(runCatching {
                    nativePerformOverlayAction(call.argument<String>("action").orEmpty())
                }.getOrElse {
                    LauncherPrefs.writeLauncherLog(this, "MainActivity.overlay performOverlayAction failed", it)
                    -100
                })
                else -> result.notImplemented()
            }
        }
        overlayChannel = channel
        engine.dartExecutor.executeDartEntrypoint(DartExecutor.DartEntrypoint.createDefault())

        val textureView = FlutterTextureView(this)
        textureView.setOpaque(false)
        val view = FlutterView(this, textureView)
        view.setBackgroundColor(Color.TRANSPARENT)
        view.attachToFlutterEngine(engine)
        val size = dp(56)
        val params = FrameLayout.LayoutParams(size, size, Gravity.TOP or Gravity.LEFT)
        params.leftMargin = 0
        params.topMargin = 0
        overlayEngine = engine
        overlayView = view
        overlayParams = params
        mFrameLayout.addView(view, params)
        mFrameLayout.post { placeOverlayAtDefault() }
    }

    private fun showFlutterOverlayMenu() {
        installFlutterGameOverlay()
        resizeOverlay(true, true)
        overlayChannel?.invokeMethod("showMenu", null)
    }

    private fun placeOverlayAtDefault() {
        val params = overlayParams ?: return
        params.leftMargin = (mFrameLayout.width - params.width).coerceAtLeast(0)
        params.topMargin = (mFrameLayout.height - params.height).coerceAtLeast(0)
        overlayView?.layoutParams = params
    }

    private fun resizeOverlay(expanded: Boolean, menuMode: Boolean) {
        val params = overlayParams ?: return
        val anchorRight = params.leftMargin + params.width
        val anchorBottom = params.topMargin + params.height
        if (expanded) {
            params.width = if (menuMode) dp(340) else dp(286)
            params.height = if (menuMode) dp(316) else dp(64)
        } else {
            params.width = dp(56)
            params.height = dp(56)
        }
        params.leftMargin = (anchorRight - params.width).coerceIn(0, (mFrameLayout.width - params.width).coerceAtLeast(0))
        params.topMargin = (anchorBottom - params.height).coerceIn(0, (mFrameLayout.height - params.height).coerceAtLeast(0))
        overlayView?.layoutParams = params
    }

    private fun moveOverlay(dx: Float, dy: Float) {
        val params = overlayParams ?: return
        params.leftMargin = (params.leftMargin + dx.toInt()).coerceIn(0, (mFrameLayout.width - params.width).coerceAtLeast(0))
        params.topMargin = (params.topMargin + dy.toInt()).coerceIn(0, (mFrameLayout.height - params.height).coerceAtLeast(0))
        overlayView?.layoutParams = params
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density + 0.5f).toInt()

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
