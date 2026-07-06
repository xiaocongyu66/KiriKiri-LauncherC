package org.github.krkr2

import android.app.Activity
import android.content.Context
import android.util.Log
import android.view.Surface
import org.libsdl.app.SDL
import java.util.concurrent.atomic.AtomicBoolean

object AndroidRuntimeBridge {
    private const val TAG = "AndroidRuntimeBridge"
    private val libraryLoaded = AtomicBoolean(false)
    private val initialized = AtomicBoolean(false)
    private val sdlJavaReady = AtomicBoolean(false)
    @Volatile private var loadFailure: Throwable? = null
    @Volatile private var initFailure: Throwable? = null

    init {
        runCatching {
            System.loadLibrary("krkr2")
            libraryLoaded.set(true)
        }.onFailure {
            loadFailure = it
            Log.e(TAG, "load libkrkr2 failed", it)
        }
    }

    @JvmStatic
    fun setApplicationContext(context: Context?) {
        if (!libraryLoaded.get()) return
        runCatching {
            nativeSetApplicationContext(context?.applicationContext)
        }.onFailure {
            initFailure = it
            Log.e(TAG, "nativeSetApplicationContext failed", it)
        }
    }

    @JvmStatic
    fun ensureInitialized(): Boolean {
        if (initialized.get()) return true
        if (!libraryLoaded.get()) return false
        return runCatching {
            nativeInitRuntime()
            initFailure = null
            initialized.set(true)
            true
        }.onFailure {
            initFailure = it
            Log.e(TAG, "native runtime init failed", it)
        }.getOrDefault(false)
    }

    @JvmStatic
    fun ensureSdlJavaReady(activity: Activity): Boolean {
        if (!ensureInitialized()) return false
        return synchronized(this) {
            runCatching {
                if (!sdlJavaReady.get()) {
                    SDL.setupJNI()
                    SDL.initialize()
                    sdlJavaReady.set(true)
                }
                SDL.setContext(activity)
                true
            }.onFailure {
                initFailure = it
                sdlJavaReady.set(false)
                Log.e(TAG, "SDL Java bootstrap failed", it)
            }.getOrDefault(false)
        }
    }

    @JvmStatic
    fun lastFailureMessage(): String =
        loadFailure?.stackTraceToString()
            ?: initFailure?.stackTraceToString()
            ?: ""

    @JvmStatic
    fun isLibraryLoaded(): Boolean = libraryLoaded.get()

    @JvmStatic
    fun isInitialized(): Boolean = initialized.get()

    @JvmStatic
    fun isSdlJavaReady(): Boolean = sdlJavaReady.get()

    @JvmStatic
    fun startGame(gamePath: String, preferenceRoot: String = ""): Boolean {
        if (!ensureInitialized()) return false
        return runCatching {
            nativeStartGame(gamePath, preferenceRoot)
        }.onFailure {
            initFailure = it
            Log.e(TAG, "nativeStartGame failed", it)
        }.getOrDefault(false)
    }

    @JvmStatic
    fun runFrame(deltaSeconds: Float) {
        if (ensureInitialized()) runCatching {
            nativeRunFrame(deltaSeconds)
        }.onFailure {
            initFailure = it
            Log.e(TAG, "nativeRunFrame failed", it)
        }
    }

    @JvmStatic
    fun pumpPresenter(): Boolean =
        ensureInitialized() && runCatching {
            nativePumpPresenter()
        }.onFailure {
            initFailure = it
            Log.e(TAG, "nativePumpPresenter failed", it)
        }.getOrDefault(false)

    @JvmStatic
    fun recordLifecycle(eventName: String, detail: String = "") {
        if (ensureInitialized()) runCatching {
            nativeLifecycleEvent(eventName, detail)
        }
    }

    @JvmStatic
    fun onLowMemory() {
        if (ensureInitialized()) runCatching {
            nativeOnLowMemory()
        }.onFailure {
            initFailure = it
            Log.e(TAG, "nativeOnLowMemory failed", it)
        }
    }

    @JvmStatic
    fun setGameSurface(surface: Surface?, width: Int, height: Int) {
        if (ensureInitialized()) runCatching {
            nativeSetGameSurface(surface, width, height)
        }.onFailure {
            initFailure = it
            Log.e(TAG, "nativeSetGameSurface failed", it)
        }
    }

    @JvmStatic
    fun resizeGameSurface(width: Int, height: Int) {
        if (ensureInitialized()) runCatching {
            nativeResizeGameSurface(width, height)
        }.onFailure {
            initFailure = it
            Log.e(TAG, "nativeResizeGameSurface failed", it)
        }
    }

    @JvmStatic
    fun detachGameSurface() {
        if (ensureInitialized()) runCatching {
            nativeDetachGameSurface()
        }.onFailure {
            initFailure = it
            Log.e(TAG, "nativeDetachGameSurface failed", it)
        }
    }

    @JvmStatic
    fun getGameSurfaceMetrics(): IntArray =
        if (ensureInitialized()) {
            runCatching {
                nativeGetGameSurfaceMetrics()
            }.onFailure {
                initFailure = it
                Log.e(TAG, "nativeGetGameSurfaceMetrics failed", it)
            }.getOrDefault(IntArray(0))
        } else {
            IntArray(0)
        }

    @JvmStatic
    fun touchBegin(id: Int, x: Float, y: Float) {
        if (ensureInitialized()) runCatching { nativeFlutterTouchesBegin(id, x, y) }
    }

    @JvmStatic
    fun touchEnd(id: Int, x: Float, y: Float) {
        if (ensureInitialized()) runCatching { nativeFlutterTouchesEnd(id, x, y) }
    }

    @JvmStatic
    fun touchMove(ids: IntArray, xs: FloatArray, ys: FloatArray) {
        if (ensureInitialized()) runCatching { nativeFlutterTouchesMove(ids, xs, ys) }
    }

    @JvmStatic
    fun touchCancel(ids: IntArray, xs: FloatArray, ys: FloatArray) {
        if (ensureInitialized()) runCatching { nativeFlutterTouchesCancel(ids, xs, ys) }
    }

    @JvmStatic
    fun keyAction(keyCode: Int, pressed: Boolean): Boolean =
        ensureInitialized() && runCatching {
            nativeKeyAction(keyCode, pressed)
        }.onFailure {
            initFailure = it
            Log.e(TAG, "nativeKeyAction failed", it)
        }.getOrDefault(false)

    @JvmStatic
    fun hoverMoved(x: Float, y: Float) {
        if (ensureInitialized()) runCatching { nativeHoverMoved(x, y) }
    }

    @JvmStatic
    fun mouseScrolled(scroll: Float) {
        if (ensureInitialized()) runCatching { nativeMouseScrolled(scroll) }
    }

    private external fun nativeInitRuntime()
    private external fun nativeSetApplicationContext(context: Context?)
    private external fun nativeStartGame(gamePath: String, preferenceRoot: String): Boolean
    private external fun nativeRunFrame(deltaSeconds: Float)
    private external fun nativePumpPresenter(): Boolean
    private external fun nativeLifecycleEvent(eventName: String, detail: String)
    private external fun nativeOnLowMemory()
    private external fun nativeSetGameSurface(surface: Surface?, width: Int, height: Int)
    private external fun nativeResizeGameSurface(width: Int, height: Int)
    private external fun nativeDetachGameSurface()
    private external fun nativeGetGameSurfaceMetrics(): IntArray
    private external fun nativeFlutterTouchesBegin(id: Int, x: Float, y: Float)
    private external fun nativeFlutterTouchesEnd(id: Int, x: Float, y: Float)
    private external fun nativeFlutterTouchesMove(ids: IntArray, xs: FloatArray, ys: FloatArray)
    private external fun nativeFlutterTouchesCancel(ids: IntArray, xs: FloatArray, ys: FloatArray)
    private external fun nativeKeyAction(keyCode: Int, pressed: Boolean): Boolean
    private external fun nativeHoverMoved(x: Float, y: Float)
    private external fun nativeMouseScrolled(scroll: Float)
}
