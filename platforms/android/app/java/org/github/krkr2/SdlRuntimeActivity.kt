package org.github.krkr2

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
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
import org.tvp.kirikiri2.NativeUiHost
import java.io.File
import java.util.Locale

class SdlRuntimeActivity : Activity(), SurfaceHolder.Callback,
    Choreographer.FrameCallback {

    companion object {
        const val EXTRA_GAME_DIR = "extra_game_dir"
        const val EXTRA_GAME_TITLE = "extra_game_title"
        const val EXTRA_LAUNCH_FILE = "extra_launch_file"
        private const val GAME_SURFACE_WIDTH = 1920
        private const val GAME_SURFACE_HEIGHT = 1080
    }

    private lateinit var gameSurfaceView: SurfaceView
    private var framePosted = false
    private var running = false
    private var surfaceReady = false
    private var gameStarted = false
    private var lastFrameNanos = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.statusBarColor = Color.BLACK
        window.navigationBarColor = Color.BLACK
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
        AndroidRuntimeBridge.setApplicationContext(applicationContext)
        if (!AndroidRuntimeBridge.ensureInitialized() ||
            !AndroidRuntimeBridge.ensureSdlJavaReady(this)
        ) {
            LauncherPrefs.writeLauncherLog(
                this,
                "SdlRuntimeActivity runtime init failed\n${AndroidRuntimeBridge.lastFailureMessage()}",
            )
        }
        recordLifecycle("onCreate")
        LauncherPrefs.writeLauncherLog(this, "SdlRuntimeActivity.onCreate")
    }

    override fun onResume() {
        super.onResume()
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
        AndroidRuntimeBridge.detachGameSurface()
        NativeUiHost.detach(this)
        ForceLandscapeHelper.release(this)
        recordLifecycle("onDestroy")
        super.onDestroy()
    }

    override fun onLowMemory() {
        super.onLowMemory()
        AndroidRuntimeBridge.onLowMemory()
        recordLifecycle("onLowMemory")
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) ForceLandscapeHelper.apply(this, true)
        recordLifecycle("onWindowFocusChanged hasFocus=$hasFocus")
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceReady = true
        holder.setFixedSize(GAME_SURFACE_WIDTH, GAME_SURFACE_HEIGHT)
        AndroidRuntimeBridge.setGameSurface(
            holder.surface,
            GAME_SURFACE_WIDTH,
            GAME_SURFACE_HEIGHT,
        )
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
        val deltaSeconds = if (lastFrameNanos == 0L) {
            1.0f / 60.0f
        } else {
            ((frameTimeNanos - lastFrameNanos).coerceAtLeast(0L) / 1_000_000_000.0f)
                .coerceIn(0.0f, 0.25f)
        }
        lastFrameNanos = frameTimeNanos
        if (gameStarted) {
            AndroidRuntimeBridge.runFrame(deltaSeconds)
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
