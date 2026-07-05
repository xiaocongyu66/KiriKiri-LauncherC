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
import android.view.Surface
import android.view.WindowManager
import android.widget.FrameLayout
import java.lang.ref.WeakReference
import java.lang.reflect.Proxy
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.embedding.engine.dart.DartExecutor
import io.flutter.embedding.android.FlutterTextureView
import io.flutter.embedding.android.FlutterView
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugins.GeneratedPluginRegistrant
import io.flutter.view.TextureRegistry
import org.libsdl.app.SDLActivity
import org.tvp.kirikiri2.KR2Activity

class MainActivity : KR2Activity() {

    companion object {
        const val EXTRA_GAME_DIR = "extra_game_dir"
        const val EXTRA_GAME_TITLE = "extra_game_title"
        const val EXTRA_LAUNCH_FILE = "extra_launch_file"
        private const val GAME_SURFACE_WIDTH = 1920
        private const val GAME_SURFACE_HEIGHT = 1080

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
    private val gameSurfaceTargets = mutableMapOf<Long, GameSurfaceTarget>()
    private var activeGameSurfaceTextureId: Long? = null

    private class GameSurfaceTarget(
        val textureId: Long,
        val mode: String,
        val producer: Any? = null,
        val legacyEntry: TextureRegistry.SurfaceTextureEntry? = null,
        var surface: Surface? = null,
        val ownsSurface: Boolean = false,
        var producerCallback: Any? = null,
    )

    private external fun nativeSetGameSurface(surface: Surface?, width: Int, height: Int)
    private external fun nativeResizeGameSurface(width: Int, height: Int)
    private external fun nativeDetachGameSurface()
    private external fun nativeGetGameSurfaceMetrics(): IntArray
    private external fun nativeGetLoadingConsoleSnapshot(): Array<String>
    private external fun nativeGetRenderOverlayStats(): Array<String>
    private external fun nativeFlutterTouchesBegin(id: Int, x: Float, y: Float)
    private external fun nativeFlutterTouchesEnd(id: Int, x: Float, y: Float)
    private external fun nativeFlutterTouchesMove(ids: IntArray, xs: FloatArray, ys: FloatArray)
    private external fun nativeFlutterTouchesCancel(ids: IntArray, xs: FloatArray, ys: FloatArray)

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

        logLifecycle("onCreate#sdl-java-ready-start")
        ensureSDLJavaReady()
        logLifecycle("onCreate#sdl-java-ready-done")
        if (!intent?.getStringExtra(EXTRA_GAME_DIR).isNullOrBlank()) {
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
        disposeGameSurfaceTextures()
        overlayView?.detachFromFlutterEngine()
        overlayEngine?.destroy()
        overlayView = null
        overlayEngine = null
        overlayChannel = null
        recordSessionTime()
        orientationListener?.disable()
        orientationListener = null
        // Restore the user's auto-rotate setting before the activity is gone.
        ForceLandscapeHelper.release(this)
        // Do NOT call SDL audio release here. SDL owns its Android audio
        // callback lifecycle, and forcing a second release during Cocos GL
        // teardown can destroy an internal audio mutex while the GLThread is
        // still draining a final frame and ends in:
        //   FORTIFY: pthread_mutex_lock called on a destroyed mutex
        //   Fatal signal 6 (SIGABRT) in tid X (GLThread Y)
        // observed in 78.log PID 14643 / GLThread 105.
        super.onDestroy()
        logLifecycle("onDestroy#after-super")
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
                    result.success(null)
                }
                "createGameSurfaceTexture" -> createGameSurfaceTexture(call, result)
                "resizeGameSurfaceTexture" -> resizeGameSurfaceTexture(call, result)
                "disposeGameSurfaceTexture" -> disposeGameSurfaceTexture(call, result)
                "getGameSurfaceMetrics" -> {
                    val metrics = nativeGetGameSurfaceMetrics()
                    result.success(
                        mapOf(
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
                    )
                }
                "getLoadingConsoleSnapshot" -> result.success(loadingConsoleSnapshotForFlutter())
                "getRenderOverlayStats" -> result.success(renderOverlayStatsForFlutter())
                "gameTouchBegin" -> {
                    nativeFlutterTouchesBegin(
                        call.argument<Int>("id") ?: 0,
                        (call.argument<Double>("x") ?: 0.0).toFloat(),
                        (call.argument<Double>("y") ?: 0.0).toFloat(),
                    )
                    result.success(null)
                }
                "gameTouchEnd" -> {
                    nativeFlutterTouchesEnd(
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
                    nativeFlutterTouchesMove(
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
                    nativeFlutterTouchesCancel(
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
        val params = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.MATCH_PARENT,
        )
        overlayEngine = engine
        overlayView = view
        overlayParams = params
        mFrameLayout.addView(view, params)
    }

    private fun showFlutterOverlayMenu() {
        installFlutterGameOverlay()
        overlayChannel?.invokeMethod("showMenu", null)
    }

    private fun loadingConsoleSnapshotForFlutter(): Map<String, Any> {
        val raw = nativeGetLoadingConsoleSnapshot()
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
            "lines" to lines
        )
    }

    private fun renderOverlayStatsForFlutter(): Map<String, Any> {
        val raw = nativeGetRenderOverlayStats()
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
            "rendererName" to raw.getOrNull(9).orEmpty()
        )
    }

    private fun placeOverlayAtDefault() {
        overlayView?.layoutParams = overlayParams
    }

    private fun resizeOverlay(expanded: Boolean, menuMode: Boolean) {
        overlayView?.layoutParams = overlayParams
    }

    private fun moveOverlay(dx: Float, dy: Float) {
        overlayView?.layoutParams = overlayParams
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density + 0.5f).toInt()

    private fun isFlutterSurfaceProducerDisabled(): Boolean {
        val value = System.getenv("KRKR2_DISABLE_FLUTTER_SURFACE_PRODUCER") ?: return false
        return value != "0" && !value.equals("false", ignoreCase = true)
    }

    private fun invokeProducerNoArg(producer: Any, name: String): Any? {
        return producer.javaClass.getMethod(name).invoke(producer)
    }

    private fun invokeProducerSetSize(producer: Any, width: Int, height: Int) {
        producer.javaClass
            .getMethod("setSize", java.lang.Integer.TYPE, java.lang.Integer.TYPE)
            .invoke(producer, width, height)
    }

    private fun invokeProducerSetCallback(producer: Any, callback: Any?) {
        val callbackClass = Class.forName("io.flutter.view.TextureRegistry\$SurfaceProducer\$Callback")
        producer.javaClass.getMethod("setCallback", callbackClass).invoke(producer, callback)
    }

    private fun createFlutterSurfaceProducer(engine: FlutterEngine): Any? {
        val renderer = engine.renderer
        var noArgFailure: Throwable? = null
        try {
            return renderer.javaClass.getMethod("createSurfaceProducer").invoke(renderer)
        } catch (error: Throwable) {
            noArgFailure = error
        }

        return try {
            val lifecycleClass = Class.forName("io.flutter.view.TextureRegistry\$SurfaceLifecycle")
            val manualLifecycle = lifecycleClass.getField("manual").get(null)
            renderer.javaClass
                .getMethod("createSurfaceProducer", lifecycleClass)
                .invoke(renderer, manualLifecycle)
        } catch (error: Throwable) {
            noArgFailure?.addSuppressed(error)
            throw noArgFailure ?: error
        }
    }

    private fun createSurfaceProducerTarget(
        engine: FlutterEngine,
        width: Int,
        height: Int,
    ): GameSurfaceTarget? {
        if (isFlutterSurfaceProducerDisabled()) return null
        var createdProducer: Any? = null
        return try {
            val producer = createFlutterSurfaceProducer(engine) ?: return null
            createdProducer = producer
            invokeProducerSetSize(producer, width, height)
            val textureId = (invokeProducerNoArg(producer, "id") as Number).toLong()
            val surface = invokeProducerNoArg(producer, "getSurface") as? Surface
                ?: error("SurfaceProducer returned null surface")
            val callbackClass = Class.forName("io.flutter.view.TextureRegistry\$SurfaceProducer\$Callback")
            val callback = Proxy.newProxyInstance(
                callbackClass.classLoader,
                arrayOf(callbackClass),
            ) { proxy, method, args ->
                when (method.name) {
                    "toString" -> "KrkrGameSurfaceProducerCallback($textureId)"
                    "hashCode" -> textureId.hashCode()
                    "equals" -> proxy === args?.getOrNull(0)
                    "onSurfaceAvailable",
                    "onSurfaceCreated" -> onGameSurfaceProducerAvailable(textureId)
                    "onSurfaceCleanup",
                    "onSurfaceDestroyed" -> onGameSurfaceProducerCleanup(textureId)
                    else -> null
                }
            }
            invokeProducerSetCallback(producer, callback)
            GameSurfaceTarget(
                textureId = textureId,
                mode = "surface-producer",
                producer = producer,
                surface = surface,
                producerCallback = callback,
            )
        } catch (error: Throwable) {
            createdProducer?.let {
                try {
                    invokeProducerNoArg(it, "release")
                } catch (_: Throwable) {
                }
            }
            LauncherPrefs.writeLauncherLog(
                this,
                "MainActivity.createSurfaceProducerTarget unavailable reason=${error.javaClass.simpleName}: ${error.message}",
            )
            null
        }
    }

    private fun createLegacySurfaceTextureTarget(
        engine: FlutterEngine,
        width: Int,
        height: Int,
    ): GameSurfaceTarget {
        val entry = engine.renderer.createSurfaceTexture()
        val textureId = entry.id()
        val surfaceTexture = entry.surfaceTexture()
        surfaceTexture.setDefaultBufferSize(width, height)
        return GameSurfaceTarget(
            textureId = textureId,
            mode = "surface-texture",
            legacyEntry = entry,
            surface = Surface(surfaceTexture),
            ownsSurface = true,
        )
    }

    private fun createGameSurfaceTarget(
        engine: FlutterEngine,
        width: Int,
        height: Int,
    ): GameSurfaceTarget {
        return createSurfaceProducerTarget(engine, width, height)
            ?: createLegacySurfaceTextureTarget(engine, width, height)
    }

    private fun setActiveGameSurface(
        target: GameSurfaceTarget,
        width: Int,
        height: Int,
        reason: String,
    ): Boolean {
        val surface = target.surface ?: return false
        activeGameSurfaceTextureId = target.textureId
        nativeSetGameSurface(surface, width, height)
        LauncherPrefs.writeLauncherLog(
            this,
            "MainActivity.$reason id=${target.textureId} mode=${target.mode} size=${width}x$height",
        )
        return true
    }

    private fun onGameSurfaceProducerAvailable(textureId: Long) {
        val target = gameSurfaceTargets[textureId] ?: return
        val producer = target.producer ?: return
        val surface = try {
            invokeProducerNoArg(producer, "getSurface") as? Surface
        } catch (error: Throwable) {
            LauncherPrefs.writeLauncherLog(
                this,
                "MainActivity.surfaceProducerAvailable failed id=$textureId reason=${error.javaClass.simpleName}: ${error.message}",
            )
            null
        } ?: return
        target.surface = surface
        setActiveGameSurface(target, GAME_SURFACE_WIDTH, GAME_SURFACE_HEIGHT, "surfaceProducerAvailable")
    }

    private fun onGameSurfaceProducerCleanup(textureId: Long) {
        val target = gameSurfaceTargets[textureId] ?: return
        target.surface = null
        if (activeGameSurfaceTextureId == textureId) {
            nativeDetachGameSurface()
            activeGameSurfaceTextureId = null
            LauncherPrefs.writeLauncherLog(this, "MainActivity.surfaceProducerCleanup id=$textureId")
        }
    }

    private fun releaseGameSurfaceTarget(target: GameSurfaceTarget) {
        if (target.producer != null) {
            try {
                invokeProducerSetCallback(target.producer, null)
            } catch (_: Throwable) {
            }
            try {
                invokeProducerNoArg(target.producer, "release")
            } catch (error: Throwable) {
                LauncherPrefs.writeLauncherLog(
                    this,
                    "MainActivity.releaseSurfaceProducer failed id=${target.textureId} reason=${error.javaClass.simpleName}: ${error.message}",
                )
            }
        } else {
            if (target.ownsSurface) {
                target.surface?.release()
            }
            target.legacyEntry?.release()
        }
        target.surface = null
        target.producerCallback = null
    }

    private fun createGameSurfaceTexture(call: MethodCall, result: MethodChannel.Result) {
        val engine = overlayEngine
        if (engine == null) {
            result.error("engine_unavailable", "Flutter overlay engine is not attached", null)
            return
        }
        val requestedWidth = call.argument<Int>("width") ?: GAME_SURFACE_WIDTH
        val requestedHeight = call.argument<Int>("height") ?: GAME_SURFACE_HEIGHT
        val width = GAME_SURFACE_WIDTH
        val height = GAME_SURFACE_HEIGHT
        val target = try {
            createGameSurfaceTarget(engine, width, height)
        } catch (error: RuntimeException) {
            result.error("texture_unavailable", "Unable to create game surface: ${error.message}", null)
            return
        }
        if (gameSurfaceTargets.isNotEmpty()) {
            nativeDetachGameSurface()
            activeGameSurfaceTextureId = null
            gameSurfaceTargets.values.forEach { releaseGameSurfaceTarget(it) }
            gameSurfaceTargets.clear()
        }
        gameSurfaceTargets[target.textureId] = target
        if (!setActiveGameSurface(target, width, height, "createGameSurfaceTexture")) {
            gameSurfaceTargets.remove(target.textureId)
            releaseGameSurfaceTarget(target)
            result.error("surface_unavailable", "Unable to acquire game surface", null)
            return
        }
        LauncherPrefs.writeLauncherLog(this, "MainActivity.createGameSurfaceTexture id=${target.textureId} mode=${target.mode} size=${width}x$height requested=${requestedWidth}x$requestedHeight")
        result.success(mapOf("textureId" to target.textureId, "width" to width, "height" to height, "surfaceMode" to target.mode))
    }

    private fun resizeGameSurfaceTexture(call: MethodCall, result: MethodChannel.Result) {
        val textureId = call.argument<Number>("textureId")?.toLong()
        if (textureId == null) {
            result.error("invalid_args", "textureId is required", null)
            return
        }
        val target = gameSurfaceTargets[textureId]
        if (target == null) {
            result.error("not_found", "Game surface $textureId not found", null)
            return
        }
        val requestedWidth = call.argument<Int>("width") ?: GAME_SURFACE_WIDTH
        val requestedHeight = call.argument<Int>("height") ?: GAME_SURFACE_HEIGHT
        val width = GAME_SURFACE_WIDTH
        val height = GAME_SURFACE_HEIGHT
        if (target.producer != null) {
            try {
                invokeProducerSetSize(target.producer, width, height)
                target.surface = invokeProducerNoArg(target.producer, "getSurface") as? Surface
            } catch (error: Throwable) {
                result.error("resize_failed", "Unable to resize SurfaceProducer: ${error.message}", null)
                return
            }
            if (!setActiveGameSurface(target, width, height, "resizeGameSurfaceTexture")) {
                result.error("surface_unavailable", "Unable to reacquire game surface", null)
                return
            }
        } else {
            target.legacyEntry?.surfaceTexture()?.setDefaultBufferSize(width, height)
            activeGameSurfaceTextureId = textureId
            nativeResizeGameSurface(width, height)
        }
        LauncherPrefs.writeLauncherLog(this, "MainActivity.resizeGameSurfaceTexture id=$textureId mode=${target.mode} size=${width}x$height requested=${requestedWidth}x$requestedHeight")
        result.success(mapOf("textureId" to textureId, "width" to width, "height" to height, "surfaceMode" to target.mode))
    }

    private fun disposeGameSurfaceTexture(call: MethodCall, result: MethodChannel.Result) {
        val textureId = call.argument<Number>("textureId")?.toLong()
        if (textureId == null) {
            result.error("invalid_args", "textureId is required", null)
            return
        }
        if (activeGameSurfaceTextureId == textureId) {
            nativeDetachGameSurface()
            activeGameSurfaceTextureId = null
        }
        gameSurfaceTargets.remove(textureId)?.let { releaseGameSurfaceTarget(it) }
        LauncherPrefs.writeLauncherLog(this, "MainActivity.disposeGameSurfaceTexture id=$textureId")
        result.success(null)
    }

    private fun disposeGameSurfaceTextures() {
        nativeDetachGameSurface()
        activeGameSurfaceTextureId = null
        gameSurfaceTargets.values.forEach { releaseGameSurfaceTarget(it) }
        gameSurfaceTargets.clear()
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
