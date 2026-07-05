package org.github.krkr2

import android.content.Context
import android.view.Surface
import java.util.concurrent.atomic.AtomicBoolean

object AndroidRuntimeBridge {
    private val initialized = AtomicBoolean(false)

    init {
        runCatching { System.loadLibrary("krkr2") }
    }

    @JvmStatic
    fun setApplicationContext(context: Context?) {
        nativeSetApplicationContext(context?.applicationContext)
    }

    @JvmStatic
    fun ensureInitialized(): Boolean {
        if (initialized.get()) return true
        return runCatching {
            nativeInitRuntime()
            initialized.set(true)
            true
        }.getOrDefault(false)
    }

    @JvmStatic
    fun startGame(gamePath: String, preferenceRoot: String = ""): Boolean {
        if (!ensureInitialized()) return false
        return nativeStartGame(gamePath, preferenceRoot)
    }

    @JvmStatic
    fun runFrame(deltaSeconds: Float) {
        if (ensureInitialized()) nativeRunFrame(deltaSeconds)
    }

    @JvmStatic
    fun pumpPresenter(): Boolean = ensureInitialized() && nativePumpPresenter()

    @JvmStatic
    fun setGameSurface(surface: Surface?, width: Int, height: Int) {
        if (ensureInitialized()) nativeSetGameSurface(surface, width, height)
    }

    @JvmStatic
    fun resizeGameSurface(width: Int, height: Int) {
        if (ensureInitialized()) nativeResizeGameSurface(width, height)
    }

    @JvmStatic
    fun detachGameSurface() {
        if (ensureInitialized()) nativeDetachGameSurface()
    }

    @JvmStatic
    fun getGameSurfaceMetrics(): IntArray =
        if (ensureInitialized()) nativeGetGameSurfaceMetrics() else IntArray(0)

    @JvmStatic
    fun touchBegin(id: Int, x: Float, y: Float) {
        if (ensureInitialized()) nativeFlutterTouchesBegin(id, x, y)
    }

    @JvmStatic
    fun touchEnd(id: Int, x: Float, y: Float) {
        if (ensureInitialized()) nativeFlutterTouchesEnd(id, x, y)
    }

    @JvmStatic
    fun touchMove(ids: IntArray, xs: FloatArray, ys: FloatArray) {
        if (ensureInitialized()) nativeFlutterTouchesMove(ids, xs, ys)
    }

    @JvmStatic
    fun touchCancel(ids: IntArray, xs: FloatArray, ys: FloatArray) {
        if (ensureInitialized()) nativeFlutterTouchesCancel(ids, xs, ys)
    }

    private external fun nativeInitRuntime()
    private external fun nativeSetApplicationContext(context: Context?)
    private external fun nativeStartGame(gamePath: String, preferenceRoot: String): Boolean
    private external fun nativeRunFrame(deltaSeconds: Float)
    private external fun nativePumpPresenter(): Boolean
    private external fun nativeSetGameSurface(surface: Surface?, width: Int, height: Int)
    private external fun nativeResizeGameSurface(width: Int, height: Int)
    private external fun nativeDetachGameSurface()
    private external fun nativeGetGameSurfaceMetrics(): IntArray
    private external fun nativeFlutterTouchesBegin(id: Int, x: Float, y: Float)
    private external fun nativeFlutterTouchesEnd(id: Int, x: Float, y: Float)
    private external fun nativeFlutterTouchesMove(ids: IntArray, xs: FloatArray, ys: FloatArray)
    private external fun nativeFlutterTouchesCancel(ids: IntArray, xs: FloatArray, ys: FloatArray)
}
