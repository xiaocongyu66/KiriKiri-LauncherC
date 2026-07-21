package org.github.krkr2

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.graphics.Color
import android.os.Bundle
import android.view.Choreographer
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import io.flutter.embedding.android.FlutterTextureView
import io.flutter.embedding.android.FlutterView
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.embedding.engine.dart.DartExecutor
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugins.GeneratedPluginRegistrant
import org.tvp.kirikiri2.NativeUiHost
import org.tvp.kirikiri2.KR2Activity
import java.io.File
import java.lang.ref.WeakReference
import java.util.Locale

class SdlRuntimeActivity : Activity(), SurfaceHolder.Callback,
    Choreographer.FrameCallback {

    companion object {
        const val EXTRA_GAME_DIR = "extra_game_dir"
        const val EXTRA_GAME_TITLE = "extra_game_title"
        const val EXTRA_LAUNCH_FILE = "extra_launch_file"
        private const val GAME_SURFACE_WIDTH = 1920
        private const val GAME_SURFACE_HEIGHT = 1080
        @Volatile private var currentActivity = WeakReference<SdlRuntimeActivity>(null)

        @JvmStatic
        fun showFlutterGameMainMenu(): Boolean {
            val activity = currentActivity.get() ?: return false
            if (activity.overlayChannel == null) return false
            activity.runOnUiThread {
                activity.overlayChannel?.invokeMethod("showMenu", null)
            }
            return true
        }
    }

    private lateinit var gameSurfaceView: SurfaceView
    private var framePosted = false
    private var running = false
    private var surfaceReady = false
    private var gameStarted = false
    private var lastFrameNanos = 0L
    private var overlayEngine: FlutterEngine? = null
    private var overlayView: FlutterView? = null
    private var overlayChannel: MethodChannel? = null
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        currentActivity = WeakReference(this)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.statusBarColor = Color.BLACK
        window.navigationBarColor = Color.BLACK
        applyImmersiveGameMode()
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE
        ForceLandscapeHelper.apply(this, true)

        val root = FrameLayout(this)
        root.setBackgroundColor(Color.BLACK)
        root.isFocusableInTouchMode = true
        gameSurfaceView = FixedAspectSurfaceView(this)
        gameSurfaceView.isFocusable = true
        gameSurfaceView.isFocusableInTouchMode = true
        gameSurfaceView.holder.setFixedSize(GAME_SURFACE_WIDTH, GAME_SURFACE_HEIGHT)
        gameSurfaceView.holder.addCallback(this)
        installTouchBridge(gameSurfaceView)
        root.addView(
            gameSurfaceView,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER,
            )
        )
        setContentView(root)
        gameSurfaceView.requestFocus()
        NativeUiHost.attach(
            this,
            root,
            gameSurfaceView,
            GAME_SURFACE_WIDTH,
            GAME_SURFACE_HEIGHT,
        )
        if (!StoragePermission.hasAccess(this)) {
            LauncherPrefs.writeLauncherLog(this, "SdlRuntimeActivity.onCreate request-storage-permission")
            startActivity(StoragePermission.manageAllFilesIntent(this))
            finish()
            return
        }
        val useFfmpegImageDecoder = LauncherPrefs.getUseFfmpegImageDecoder(this)
        val ffmpegDecodeMode = LauncherPrefs.getFfmpegDecodeMode(this)
        AndroidRuntimeBridge.setApplicationContext(applicationContext)
        val runtimeReady = AndroidRuntimeBridge.ensureInitialized()
        val nativeLogFile = if (runtimeReady) {
            LauncherPrefs.configureNativeLogging(this)
        } else {
            LauncherPrefs.beginUnifiedLogSession(this)
        }
        if (runtimeReady) {
            KR2Activity.setUseFFmpegImageDecoder(useFfmpegImageDecoder)
            KR2Activity.setFFmpegDecodeMode(LauncherPrefs.getFfmpegDecodeModeCode(this))
        }
        if (!runtimeReady || !AndroidRuntimeBridge.ensureSdlJavaReady(this)) {
            LauncherPrefs.writeLauncherLog(
                this,
                "SdlRuntimeActivity runtime init failed\n${AndroidRuntimeBridge.lastFailureMessage()}",
            )
            finish()
            return
        }
        installFlutterGameOverlay(root)
        LauncherPrefs.writeLauncherLog(this, "SdlRuntimeActivity.flutterOverlay enabled: input/menu only")
        recordLifecycle("onCreate")
        LauncherPrefs.writeLauncherLog(
            this,
            "SdlRuntimeActivity.onCreate nativeLogFile=$nativeLogFile " +
                "ffmpegImageDecoder=$useFfmpegImageDecoder ffmpegDecodeMode=$ffmpegDecodeMode",
        )
    }

    override fun onResume() {
        super.onResume()
        applyImmersiveGameMode()
        running = true
        lastFrameNanos = 0L
        AndroidRuntimeBridge.ensureSdlJavaReady(this)
        recordLifecycle("onResume")
        postFramePump()
    }

    override fun onPause() {
        recordLifecycle("onPause#enter")
        running = false
        removeFramePump()
        super.onPause()
        recordLifecycle("onPause#after-super")
    }

    override fun onDestroy() {
        running = false
        removeFramePump()
        overlayChannel?.setMethodCallHandler(null)
        recordLifecycle("onDestroy#enter")
        AndroidRuntimeBridge.detachGameSurface()
        overlayView?.detachFromFlutterEngine()
        overlayEngine?.destroy()
        overlayView = null
        overlayEngine = null
        overlayChannel = null
        currentActivity = WeakReference(null)
        AndroidRuntimeBridge.clearSdlContext(this)
        NativeUiHost.detach(this)
        ForceLandscapeHelper.release(this)
        super.onDestroy()
    }

    override fun onLowMemory() {
        super.onLowMemory()
        AndroidRuntimeBridge.onLowMemory()
        recordLifecycle("onLowMemory")
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            applyImmersiveGameMode()
            ForceLandscapeHelper.apply(this, true)
        }
        recordLifecycle("onWindowFocusChanged hasFocus=$hasFocus")
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        holder.setFixedSize(GAME_SURFACE_WIDTH, GAME_SURFACE_HEIGHT)
        surfaceReady = attachSurfaceViewIfValid("surfaceCreated")
        recordLifecycle("surfaceCreated")
        startGameIfReady()
        postFramePump()
    }

    override fun surfaceChanged(
        holder: SurfaceHolder,
        format: Int,
        width: Int,
        height: Int,
    ) {
        holder.setFixedSize(GAME_SURFACE_WIDTH, GAME_SURFACE_HEIGHT)
        AndroidRuntimeBridge.resizeGameSurface(GAME_SURFACE_WIDTH, GAME_SURFACE_HEIGHT)
        recordLifecycle("surfaceChanged requested=${width}x$height")
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false
        AndroidRuntimeBridge.detachGameSurface()
        recordLifecycle("surfaceDestroyed")
    }

    override fun doFrame(frameTimeNanos: Long) {
        framePosted = false
        if (!running) return
        lastFrameNanos = frameTimeNanos
        if (gameStarted) {
            AndroidRuntimeBridge.runFrame()
        }
        postFramePump()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (isRuntimeKey(keyCode)) {
            AndroidRuntimeBridge.keyAction(keyCode, true)
            return true
        }
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (isRuntimeKey(keyCode)) {
            AndroidRuntimeBridge.keyAction(keyCode, false)
            return true
        }
        return super.onKeyUp(keyCode, event)
    }

    private fun startGameIfReady() {
        if (gameStarted || !surfaceReady) return
        if (!AndroidRuntimeBridge.ensureSdlJavaReady(this)) {
            LauncherPrefs.writeLauncherLog(
                this,
                "SdlRuntimeActivity.start skipped runtime not ready\n" +
                    AndroidRuntimeBridge.lastFailureMessage(),
            )
            finish()
            return
        }
        val gameDir = intent?.getStringExtra(EXTRA_GAME_DIR).orEmpty()
        val launchPath = resolveLaunchPath(gameDir)
        if (launchPath.isBlank()) {
            LauncherPrefs.writeLauncherLog(this, "SdlRuntimeActivity.start skipped empty launch path")
            return
        }
        gameStarted = AndroidRuntimeBridge.startGame(launchPath, gameDir)
        LauncherPrefs.writeLauncherLog(
            this,
            "SdlRuntimeActivity.startGame result=$gameStarted path=$launchPath",
        )
        if (!gameStarted) {
            LauncherPrefs.writeLauncherLog(this, "SdlRuntimeActivity.start failed no legacy Cocos fallback")
            finish()
        }
    }

    @Suppress("DEPRECATION")
    private fun applyImmersiveGameMode() {
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
    }

    private fun resolveLaunchPath(gameDir: String): String {
        val explicit = intent?.getStringExtra(EXTRA_LAUNCH_FILE).orEmpty()
        if (explicit.isNotBlank()) {
            val file = File(explicit)
            if (file.isFile && file.canRead()) return file.absolutePath
        }
        if (gameDir.isBlank()) return ""
        val root = File(gameDir)
        if (root.isFile && root.canRead()) return root.absolutePath
        if (!root.isDirectory) return ""
        File(root, "startup.tjs").let { if (it.isFile && it.canRead()) return it.absolutePath }
        File(root, "start.tjs").let { if (it.isFile && it.canRead()) return it.absolutePath }
        val dataXp3 = File(gameDir, "data.xp3")
        if (dataXp3.isFile && dataXp3.canRead()) return dataXp3.absolutePath
        return root.listFiles()
            ?.asSequence()
            ?.filter {
                it.isFile && it.canRead() &&
                    isLaunchExtension(it.extension.lowercase(Locale.ROOT))
            }
            ?.sortedWith(compareBy<File> { launchRank(it) }.thenBy {
                it.name.lowercase(Locale.ROOT)
            })
            ?.firstOrNull()
            ?.absolutePath
            .orEmpty()
    }

    private fun isLaunchExtension(extension: String): Boolean =
        extension == "xp3" || extension == "tjs" || extension == "ks"

    private fun launchRank(file: File): Int {
        val name = file.name.lowercase(Locale.ROOT)
        preferredLaunchRank(name).takeIf { it >= 0 }?.let { return it }
        val base = file.nameWithoutExtension.lowercase(Locale.ROOT)
        return when (file.extension.lowercase(Locale.ROOT)) {
            "xp3" -> when {
                base == "boot" -> 20
                base == "main" || base == "game" ||
                    base == "scenario" || base == "script" -> 30
                base.startsWith("data") -> 40
                isAssetArchiveBase(base) -> 300
                else -> 80
            }
            "tjs" -> if (base == "main" || base == "boot" || base == "game") 60 else 90
            "ks" -> if (base == "first" || base == "scenario") 70 else 100
            else -> 500
        }
    }

    private fun preferredLaunchRank(name: String): Int =
        when (name) {
            "startup.tjs" -> 0
            "start.tjs" -> 1
            "data.xp3" -> 2
            "startup.xp3" -> 3
            "start.xp3" -> 4
            "main.xp3" -> 5
            "game.xp3" -> 6
            "first.ks" -> 7
            "scenario.ks" -> 8
            else -> -1
        }

    private fun isAssetArchiveBase(base: String): Boolean =
        base == "patch" ||
            base.startsWith("patch") ||
            base == "bg" ||
            base.startsWith("bg") ||
            base.contains("image") ||
            base.contains("voice") ||
            base.contains("sound") ||
            base.contains("audio") ||
            base.contains("music") ||
            base.contains("movie") ||
            base.contains("video") ||
            base.contains("effect")

    private fun postFramePump() {
        if (!running || framePosted) return
        framePosted = true
        Choreographer.getInstance().postFrameCallback(this)
    }

    private fun removeFramePump() {
        if (!framePosted) return
        Choreographer.getInstance().removeFrameCallback(this)
        framePosted = false
    }

    private fun installFlutterGameOverlay(root: FrameLayout) {
        if (overlayView != null) return
        val engine = FlutterEngine(this)
        engine.navigationChannel.setInitialRoute("/game-overlay")
        GeneratedPluginRegistrant.registerWith(engine)
        val channel = MethodChannel(engine.dartExecutor.binaryMessenger, "org.github.krkr2/game_overlay")
        channel.setMethodCallHandler { call, result ->
            when (call.method) {
                "move",
                "setExpanded" -> result.success(null)
                "getGameSurfaceMetrics" -> result.success(gameSurfaceMetricsForFlutter())
                "getLoadingConsoleSnapshot" -> result.success(loadingConsoleSnapshotForFlutter())
                "getRenderOverlayStats" -> result.success(renderOverlayStatsForFlutter())
                "gameTouchBegin" -> {
                    AndroidRuntimeBridge.touchBegin(
                        call.argument<Int>("id") ?: 0,
                        (call.argument<Double>("x") ?: 0.0).toFloat(),
                        (call.argument<Double>("y") ?: 0.0).toFloat(),
                    )
                    result.success(null)
                }
                "gameTouchEnd" -> {
                    AndroidRuntimeBridge.touchEnd(
                        call.argument<Int>("id") ?: 0,
                        (call.argument<Double>("x") ?: 0.0).toFloat(),
                        (call.argument<Double>("y") ?: 0.0).toFloat(),
                    )
                    result.success(null)
                }
                "gameTouchMove" -> {
                    val ids = call.argument<List<Number>>("ids").orEmpty()
                    val xs = call.argument<List<Number>>("xs").orEmpty()
                    val ys = call.argument<List<Number>>("ys").orEmpty()
                    val count = minOf(ids.size, xs.size, ys.size)
                    AndroidRuntimeBridge.touchMove(
                        IntArray(count) { ids[it].toInt() },
                        FloatArray(count) { xs[it].toFloat() },
                        FloatArray(count) { ys[it].toFloat() },
                    )
                    result.success(null)
                }
                "gameTouchCancel" -> {
                    val ids = call.argument<List<Number>>("ids").orEmpty()
                    val xs = call.argument<List<Number>>("xs").orEmpty()
                    val ys = call.argument<List<Number>>("ys").orEmpty()
                    val count = minOf(ids.size, xs.size, ys.size)
                    AndroidRuntimeBridge.touchCancel(
                        IntArray(count) { ids[it].toInt() },
                        FloatArray(count) { xs[it].toFloat() },
                        FloatArray(count) { ys[it].toFloat() },
                    )
                    result.success(null)
                }
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
        overlayEngine = engine
        overlayView = view
        root.addView(
            view,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT,
            ),
        )
    }

    private fun gameSurfaceMetricsForFlutter(): Map<String, Any> {
        val metrics = AndroidRuntimeBridge.getGameSurfaceMetrics()
        return mapOf(
            "width" to metrics.getOrElse(4) { 0 },
            "height" to metrics.getOrElse(5) { 0 },
            "contentWidth" to metrics.getOrElse(4) { 0 },
            "contentHeight" to metrics.getOrElse(5) { 0 },
            "presentedWidth" to metrics.getOrElse(0) { 0 },
            "presentedHeight" to metrics.getOrElse(1) { 0 },
            "surfaceWidth" to metrics.getOrElse(2) { 0 },
            "surfaceHeight" to metrics.getOrElse(3) { 0 },
            "viewportX" to metrics.getOrElse(6) { 0 },
            "viewportY" to metrics.getOrElse(7) { 0 },
            "viewportWidth" to metrics.getOrElse(8) { 0 },
            "viewportHeight" to metrics.getOrElse(9) { 0 },
        )
    }

    private fun loadingConsoleSnapshotForFlutter(): Map<String, Any> {
        val raw = AndroidRuntimeBridge.getLoadingConsoleSnapshot()
        val meta = raw.getOrNull(0)?.split('\t', limit = 3).orEmpty()
        val lines = raw.drop(1).map { entry ->
            val splitAt = entry.indexOf('\t')
            val important = splitAt >= 0 && entry.substring(0, splitAt) == "1"
            val message = if (splitAt >= 0) entry.substring(splitAt + 1) else entry
            mapOf("important" to important, "message" to message)
        }
        return mapOf(
            "active" to (meta.getOrNull(0) == "1"),
            "session" to (meta.getOrNull(1)?.toLongOrNull() ?: 0L),
            "totalLines" to (meta.getOrNull(2)?.toLongOrNull() ?: 0L),
            "lines" to lines,
        )
    }

    private fun renderOverlayStatsForFlutter(): Map<String, Any> {
        val raw = AndroidRuntimeBridge.getRenderOverlayStats()
        return mapOf(
            "showFps" to (raw.getOrNull(0) == "1"),
            "available" to (raw.getOrNull(1) == "1"),
            "fps" to (raw.getOrNull(2)?.toDoubleOrNull() ?: 0.0),
            "drawCount" to (raw.getOrNull(3)?.toLongOrNull() ?: 0L),
            "videoMemoryBytes" to (raw.getOrNull(4)?.toLongOrNull() ?: 0L),
            "selfMemoryMb" to (raw.getOrNull(5)?.toIntOrNull() ?: 0),
            "freeMemoryMb" to (raw.getOrNull(6)?.toIntOrNull() ?: 0),
            "presentedFrames" to (raw.getOrNull(7)?.toLongOrNull() ?: 0L),
            "sequence" to (raw.getOrNull(8)?.toLongOrNull() ?: 0L),
            "rendererName" to raw.getOrNull(9).orEmpty(),
        )
    }

    private fun attachSurfaceViewIfValid(reason: String): Boolean {
        val surface = gameSurfaceView.holder.surface
        if (surface == null || !surface.isValid) {
            LauncherPrefs.writeLauncherLog(this, "SdlRuntimeActivity.$reason surface-view invalid")
            return false
        }
        AndroidRuntimeBridge.setGameSurface(surface, GAME_SURFACE_WIDTH, GAME_SURFACE_HEIGHT)
        LauncherPrefs.writeLauncherLog(this, "SdlRuntimeActivity.$reason surface-view size=${GAME_SURFACE_WIDTH}x$GAME_SURFACE_HEIGHT")
        return true
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun installTouchBridge(view: SurfaceView) {
        view.setOnTouchListener { touchedView, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN,
                MotionEvent.ACTION_POINTER_DOWN -> {
                    val index = event.actionIndex
                    val x = mapSurfaceX(touchedView.width, event.getX(index))
                    val y = mapSurfaceY(touchedView.height, event.getY(index))
                    AndroidRuntimeBridge.touchBegin(event.getPointerId(index), x, y)
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val count = event.pointerCount
                    val ids = IntArray(count)
                    val xs = FloatArray(count)
                    val ys = FloatArray(count)
                    for (i in 0 until count) {
                        ids[i] = event.getPointerId(i)
                        xs[i] = mapSurfaceX(touchedView.width, event.getX(i))
                        ys[i] = mapSurfaceY(touchedView.height, event.getY(i))
                    }
                    AndroidRuntimeBridge.touchMove(ids, xs, ys)
                    true
                }
                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_POINTER_UP -> {
                    val index = event.actionIndex
                    val x = mapSurfaceX(touchedView.width, event.getX(index))
                    val y = mapSurfaceY(touchedView.height, event.getY(index))
                    AndroidRuntimeBridge.touchEnd(event.getPointerId(index), x, y)
                    touchedView.performClick()
                    true
                }
                MotionEvent.ACTION_CANCEL -> {
                    val count = event.pointerCount
                    val ids = IntArray(count)
                    val xs = FloatArray(count)
                    val ys = FloatArray(count)
                    for (i in 0 until count) {
                        ids[i] = event.getPointerId(i)
                        xs[i] = mapSurfaceX(touchedView.width, event.getX(i))
                        ys[i] = mapSurfaceY(touchedView.height, event.getY(i))
                    }
                    AndroidRuntimeBridge.touchCancel(ids, xs, ys)
                    true
                }
                else -> true
            }
        }
        view.setOnHoverListener { hoveredView, event ->
            if (event.actionMasked == MotionEvent.ACTION_HOVER_MOVE) {
                AndroidRuntimeBridge.hoverMoved(
                    mapSurfaceX(hoveredView.width, event.x),
                    mapSurfaceY(hoveredView.height, event.y),
                )
                true
            } else {
                false
            }
        }
        view.setOnGenericMotionListener { motionView, event ->
            if (event.actionMasked == MotionEvent.ACTION_SCROLL) {
                AndroidRuntimeBridge.mouseScrolled(-event.getAxisValue(MotionEvent.AXIS_VSCROLL))
                true
            } else if (event.actionMasked == MotionEvent.ACTION_HOVER_MOVE) {
                AndroidRuntimeBridge.hoverMoved(
                    mapSurfaceX(motionView.width, event.x),
                    mapSurfaceY(motionView.height, event.y),
                )
                true
            } else {
                false
            }
        }
    }

    private fun isRuntimeKey(keyCode: Int): Boolean =
        when (keyCode) {
            KeyEvent.KEYCODE_BACK,
            KeyEvent.KEYCODE_MENU,
            KeyEvent.KEYCODE_DPAD_LEFT,
            KeyEvent.KEYCODE_DPAD_RIGHT,
            KeyEvent.KEYCODE_DPAD_UP,
            KeyEvent.KEYCODE_DPAD_DOWN,
            KeyEvent.KEYCODE_ENTER,
            KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE,
            KeyEvent.KEYCODE_DPAD_CENTER,
            KeyEvent.KEYCODE_DEL -> true
            else -> false
        }

    private fun recordLifecycle(event: String) {
        val gameDir = intent?.getStringExtra(EXTRA_GAME_DIR).orEmpty()
        val launchFile = intent?.getStringExtra(EXTRA_LAUNCH_FILE).orEmpty()
        AndroidRuntimeBridge.recordLifecycle(
            "SdlRuntimeActivity.$event",
            "gameDir=$gameDir launchFile=$launchFile surface=$surfaceReady " +
                "started=$gameStarted running=$running thread=${Thread.currentThread().name}",
        )
    }

    private fun mapSurfaceX(viewWidth: Int, x: Float): Float =
        if (viewWidth > 0) x * GAME_SURFACE_WIDTH / viewWidth else x

    private fun mapSurfaceY(viewHeight: Int, y: Float): Float =
        if (viewHeight > 0) y * GAME_SURFACE_HEIGHT / viewHeight else y

    private class FixedAspectSurfaceView(context: Context) : SurfaceView(context) {
        override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
            val maxWidth = View.MeasureSpec.getSize(widthMeasureSpec)
            val maxHeight = View.MeasureSpec.getSize(heightMeasureSpec)
            if (maxWidth <= 0 || maxHeight <= 0) {
                super.onMeasure(widthMeasureSpec, heightMeasureSpec)
                return
            }
            var width = maxWidth
            var height = (width.toLong() * GAME_SURFACE_HEIGHT / GAME_SURFACE_WIDTH).toInt()
            if (height > maxHeight) {
                height = maxHeight
                width = (height.toLong() * GAME_SURFACE_WIDTH / GAME_SURFACE_HEIGHT).toInt()
            }
            setMeasuredDimension(width, height)
        }
    }
}
