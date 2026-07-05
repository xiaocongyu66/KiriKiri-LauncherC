package org.github.krkr2

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.content.pm.ActivityInfo
import android.graphics.Color
import android.os.Bundle
import android.view.Choreographer
import android.view.Gravity
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import java.io.File

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
        gameSurfaceView = FixedAspectSurfaceView(this)
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
        AndroidRuntimeBridge.setApplicationContext(applicationContext)
        AndroidRuntimeBridge.ensureInitialized()
        LauncherPrefs.writeLauncherLog(this, "SdlRuntimeActivity.onCreate")
    }

    override fun onResume() {
        super.onResume()
        running = true
        lastFrameNanos = 0L
        postFramePump()
    }

    override fun onPause() {
        running = false
        removeFramePump()
        super.onPause()
    }

    override fun onDestroy() {
        running = false
        removeFramePump()
        AndroidRuntimeBridge.detachGameSurface()
        ForceLandscapeHelper.release(this)
        super.onDestroy()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceReady = true
        holder.setFixedSize(GAME_SURFACE_WIDTH, GAME_SURFACE_HEIGHT)
        AndroidRuntimeBridge.setGameSurface(
            holder.surface,
            GAME_SURFACE_WIDTH,
            GAME_SURFACE_HEIGHT,
        )
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
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false
        AndroidRuntimeBridge.detachGameSurface()
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

    private fun startGameIfReady() {
        if (gameStarted || !surfaceReady) return
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
        if (explicit.isNotBlank()) return explicit
        if (gameDir.isBlank()) return ""
        val dataXp3 = File(gameDir, "data.xp3")
        return if (dataXp3.isFile && dataXp3.canRead()) dataXp3.absolutePath else gameDir
    }

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
