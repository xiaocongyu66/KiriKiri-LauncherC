package org.github.krkr2

import java.util.concurrent.atomic.AtomicBoolean

object AndroidRuntimeBridge {
    private val initialized = AtomicBoolean(false)

    init {
        runCatching { System.loadLibrary("krkr2") }
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

    private external fun nativeInitRuntime()
    private external fun nativeStartGame(gamePath: String, preferenceRoot: String): Boolean
    private external fun nativeRunFrame(deltaSeconds: Float)
    private external fun nativePumpPresenter(): Boolean
}
