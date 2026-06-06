package org.github.krkr2
import android.content.Context
import android.util.Log
import java.io.File
import java.io.PrintWriter
import java.io.RandomAccessFile
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
object LauncherPrefs {
    private const val TAG = "KR2LauncherPrefs"
    private const val PREF = "krkr2_launcher"
    private const val KEY_GAME_ROOT = "game_root"
    private const val KEY_LAST_GAME = "last_game"
    private const val KEY_LANGUAGE = "language"
    private const val KEY_FORCE_LANDSCAPE = "force_landscape"
    private const val KEY_USE_FFMPEG_IMAGE_DECODER = "use_ffmpeg_image_decoder"
    private const val KEY_FFMPEG_DECODE_MODE = "ffmpeg_decode_mode"
    private const val KEY_FILE_LOG_ENABLED = "file_log_enabled"
    private const val KEY_FILE_LOG_AUTO_CLEANUP = "file_log_auto_cleanup"
    private const val KEY_FILE_LOG_RETENTION_DAYS = "file_log_retention_days"
    private const val KEY_ACTIVE_LOG_FILE = "active_log_file"
    const val ENGINE_KEY_FFMPEG_IMAGE_DECODER = "ffmpeg_image_decoder"
    const val ENGINE_KEY_FFMPEG_DECODE_MODE = "ffmpeg_decode_mode"
    const val FFMPEG_DECODE_MODE_SOFTWARE = "software"
    const val FFMPEG_DECODE_MODE_HARDWARE = "hardware"
    // Maximum directory depth GameScanner walks below the configured root
    // before giving up. Deeper trees take quadratically longer, especially
    // when MANAGE_EXTERNAL_STORAGE is granted and the root happens to be
    // /storage/emulated/0. Tyranor uses 2 by default — keep parity unless
    // the user opts into a deeper walk via Settings.
    private const val KEY_SCAN_DEPTH = "scan_depth"
    const val SCAN_DEPTH_MIN = 1
    const val SCAN_DEPTH_MAX = 10
    const val SCAN_DEPTH_DEFAULT = 2
    // Snapshot of Settings.System.ACCELEROMETER_ROTATION captured by
    // ForceLandscapeHelper before it disabled auto-rotate. -1 means "no
    // saved value, do not restore". We persist this so a process kill
    // mid-game still lets us revert auto-rotate the next time the launcher
    // starts cleanly.
    private const val KEY_SAVED_ACCEL_ROTATION = "saved_accelerometer_rotation"

    const val LANG_EN = "en"
    const val LANG_ZH = "zh"
    const val DEFAULT_GAME_ROOT = "/storage/emulated/0/krkr2pro"
    const val NATIVE_LOG_DIR = "/storage/emulated/0/krkr2pro/logs"
    const val FILE_LOG_RETENTION_DEFAULT_DAYS = 15
    const val FILE_LOG_RETENTION_MIN_DAYS = 1
    const val FILE_LOG_RETENTION_MAX_DAYS = 60
    private const val UNIFIED_LOG_NAME_PATTERN = "\\d{12}(?:\\d{2}(?:\\d{3})?)?\\.log"
    private val unifiedLogNameRegex = Regex(UNIFIED_LOG_NAME_PATTERN)
    @Volatile private var nativeFileLoggingConfigured = false
    private val unifiedLogLock = Any()

    fun getGameRoot(context: Context): String = LauncherSettingsDb.getString(context, KEY_GAME_ROOT, DEFAULT_GAME_ROOT)

    fun setGameRoot(context: Context, path: String) {
        LauncherSettingsDb.setString(context, KEY_GAME_ROOT, path.trim().ifBlank { DEFAULT_GAME_ROOT })
    }

    fun getLanguage(context: Context): String = LauncherSettingsDb.getString(context, KEY_LANGUAGE, LANG_EN)

    fun setLanguage(context: Context, language: String) {
        LauncherSettingsDb.setString(context, KEY_LANGUAGE, if (language == LANG_ZH) LANG_ZH else LANG_EN)
    }

    fun getForceLandscape(context: Context): Boolean = LauncherSettingsDb.getBoolean(context, KEY_FORCE_LANDSCAPE, true)

    fun setForceLandscape(context: Context, enabled: Boolean) {
        LauncherSettingsDb.setBoolean(context, KEY_FORCE_LANDSCAPE, enabled)
    }

    fun getUseFfmpegImageDecoder(context: Context): Boolean =
        LauncherSettingsDb.getBoolean(
            context,
            KEY_USE_FFMPEG_IMAGE_DECODER,
            KrkrPrefsStore.getBool(context, ENGINE_KEY_FFMPEG_IMAGE_DECODER, false),
        )

    fun setUseFfmpegImageDecoder(context: Context, enabled: Boolean) {
        LauncherSettingsDb.setBoolean(context, KEY_USE_FFMPEG_IMAGE_DECODER, enabled)
        KrkrPrefsStore.setBool(context, ENGINE_KEY_FFMPEG_IMAGE_DECODER, enabled)
        writeLauncherLog(context, "FFmpeg image decoder enabled=$enabled")
    }

    fun getFfmpegDecodeMode(context: Context): String {
        val mode = LauncherSettingsDb.getString(
            context,
            KEY_FFMPEG_DECODE_MODE,
            KrkrPrefsStore.getString(context, ENGINE_KEY_FFMPEG_DECODE_MODE, FFMPEG_DECODE_MODE_SOFTWARE),
        )
        return normalizeFfmpegDecodeMode(mode)
    }

    fun getFfmpegDecodeModeCode(context: Context): Int =
        if (getFfmpegDecodeMode(context) == FFMPEG_DECODE_MODE_HARDWARE) 1 else 0

    fun setFfmpegDecodeMode(context: Context, mode: String) {
        val normalized = normalizeFfmpegDecodeMode(mode)
        LauncherSettingsDb.setString(context, KEY_FFMPEG_DECODE_MODE, normalized)
        KrkrPrefsStore.setString(context, ENGINE_KEY_FFMPEG_DECODE_MODE, normalized)
        writeLauncherLog(context, "FFmpeg decode mode=$normalized")
    }

    private fun normalizeFfmpegDecodeMode(mode: String): String =
        if (mode == FFMPEG_DECODE_MODE_HARDWARE) FFMPEG_DECODE_MODE_HARDWARE else FFMPEG_DECODE_MODE_SOFTWARE

    fun getFileLogEnabled(context: Context): Boolean =
        LauncherSettingsDb.getBoolean(context, KEY_FILE_LOG_ENABLED, true)

    fun setFileLogEnabled(context: Context, enabled: Boolean) {
        LauncherSettingsDb.setBoolean(context, KEY_FILE_LOG_ENABLED, enabled)
        writeLauncherLog(context, "Native file logging enabled=$enabled")
    }

    fun getFileLogAutoCleanup(context: Context): Boolean =
        LauncherSettingsDb.getBoolean(context, KEY_FILE_LOG_AUTO_CLEANUP, true)

    fun setFileLogAutoCleanup(context: Context, enabled: Boolean) {
        LauncherSettingsDb.setBoolean(context, KEY_FILE_LOG_AUTO_CLEANUP, enabled)
        writeLauncherLog(context, "Native log auto cleanup enabled=$enabled")
    }

    fun getFileLogRetentionDays(context: Context): Int =
        LauncherSettingsDb.getInt(
            context,
            KEY_FILE_LOG_RETENTION_DAYS,
            FILE_LOG_RETENTION_DEFAULT_DAYS,
        ).coerceIn(FILE_LOG_RETENTION_MIN_DAYS, FILE_LOG_RETENTION_MAX_DAYS)

    fun setFileLogRetentionDays(context: Context, days: Int) {
        LauncherSettingsDb.setInt(
            context,
            KEY_FILE_LOG_RETENTION_DAYS,
            days.coerceIn(FILE_LOG_RETENTION_MIN_DAYS, FILE_LOG_RETENTION_MAX_DAYS),
        )
    }

    fun configureNativeLogging(context: Context): String {
        val enabled = getFileLogEnabled(context)
        val logFile = if (enabled) activeUnifiedLogFile(context) ?: beginUnifiedLogSession(context) else ""
        nativeFileLoggingConfigured = runCatching {
            org.tvp.kirikiri2.KR2Activity.configureFileLogging(enabled, logFile)
            enabled && logFile.isNotBlank()
        }.onFailure {
            Log.w(TAG, "configureFileLogging failed", it)
        }.getOrDefault(false)
        return logFile
    }

    fun beginUnifiedLogSession(context: Context): String {
        if (!getFileLogEnabled(context)) return ""
        return prepareNativeLogFile(context)
    }

    private fun prepareNativeLogFile(context: Context): String {
        val dir = File(NATIVE_LOG_DIR)
        if (!dir.exists()) dir.mkdirs()
        cleanupOldUnifiedLogs(context)
        val name = SimpleDateFormat("yyyyMMddHHmmssSSS", Locale.US).format(Date()) + ".log"
        val file = File(dir, name)
        LauncherSettingsDb.setString(context, KEY_ACTIVE_LOG_FILE, file.absolutePath)
        nativeFileLoggingConfigured = false
        appendUnifiedLog(context, "native log session path=${file.absolutePath}")
        return file.absolutePath
    }

    private fun activeUnifiedLogFile(context: Context): String? {
        val active = LauncherSettingsDb.getString(context, KEY_ACTIVE_LOG_FILE, "")
            .takeIf { it.isNotBlank() }
            ?: return null
        return active.takeIf { File(it).exists() }
    }

    fun getScanDepth(context: Context): Int = LauncherSettingsDb.getInt(context, KEY_SCAN_DEPTH, SCAN_DEPTH_DEFAULT).coerceIn(SCAN_DEPTH_MIN, SCAN_DEPTH_MAX)

    fun setScanDepth(context: Context, depth: Int) {
        LauncherSettingsDb.setInt(context, KEY_SCAN_DEPTH, depth.coerceIn(SCAN_DEPTH_MIN, SCAN_DEPTH_MAX))
    }

    fun getSavedAccelerometerRotation(context: Context): Int = LauncherSettingsDb.getInt(context, KEY_SAVED_ACCEL_ROTATION, -1)

    fun setSavedAccelerometerRotation(context: Context, value: Int) {
        LauncherSettingsDb.setInt(context, KEY_SAVED_ACCEL_ROTATION, value)
    }

    fun getLastGamePath(context: Context): String? = LauncherSettingsDb.getString(context, KEY_LAST_GAME, "").takeIf { it.isNotBlank() }

    fun setLastGamePath(context: Context, path: String) {
        LauncherSettingsDb.setString(context, KEY_LAST_GAME, path)
    }

    fun ensureGameUuid(context: Context, gameDir: String): String = GamePrefsDb.ensureGameUuid(context, gameDir)

    fun getGameUuid(context: Context, gameDir: String): String = GamePrefsDb.ensureGameUuid(context, gameDir)

    fun getAlias(context: Context, gameDir: String): String? = GamePrefsDb.getGamePref(context, gameDir, "alias")

    fun getCustomImagePath(context: Context, gameDir: String): String? = GamePrefsDb.getGamePref(context, gameDir, "custom_image")

    fun setCustomImagePath(context: Context, gameDir: String, imagePath: String) {
        GamePrefsDb.putGamePref(context, gameDir, "custom_image", imagePath.trim().ifBlank { null })
    }

    /**
     * Per-game override for the launch file (the .xp3/.tjs/.ks the engine
     * boots from). Empty/null means "auto detect" — KR2Activity falls back
     * to startup.tjs / start.tjs / data.xp3 / first .xp3 / first .ks under
     * the game directory.
     */
    fun getCustomLaunchFile(context: Context, gameDir: String): String? =
        GamePrefsDb.getGamePref(context, gameDir, "custom_launch")

    fun setCustomLaunchFile(context: Context, gameDir: String, path: String) {
        GamePrefsDb.putGamePref(context, gameDir, "custom_launch", path.trim().ifBlank { null })
    }


    private val GAME_ENGINE_KEYS = listOf("renderer", "fps_limit", "showfps", "ogl_accurate_render")

    fun getGameEnginePref(context: Context, gameDir: String, key: String): String? =
        GamePrefsDb.getGamePref(context, gameDir, "engine_$key")

    fun setGameEnginePref(context: Context, gameDir: String, key: String, value: String?) {
        GamePrefsDb.putGamePref(context, gameDir, "engine_$key", value?.trim()?.ifBlank { null })
    }

    fun clearGameEnginePrefs(context: Context, gameDir: String) {
        GAME_ENGINE_KEYS.forEach { setGameEnginePref(context, gameDir, it, null) }
    }

    fun applyGameEngineOverrides(context: Context, gameDir: String) {
        val updates = GAME_ENGINE_KEYS.mapNotNull { key ->
            getGameEnginePref(context, gameDir, key)?.takeIf { it.isNotBlank() }?.let { key to it }
        }.toMap()
        if (updates.isNotEmpty()) {
            KrkrPrefsStore.update(context, updates)
            writeLauncherLog(context, "Applied per-game engine overrides for $gameDir: ${updates.keys.joinToString()}")
        }
    }

    fun setAlias(context: Context, gameDir: String, alias: String) {
        GamePrefsDb.putGamePref(context, gameDir, "alias", alias.trim().ifBlank { null })
    }

    fun displayName(context: Context, game: GameEntry): String {
        return getAlias(context, game.gameDir)?.takeIf { it.isNotBlank() } ?: game.title
    }

    fun recordLaunch(context: Context, gameDir: String) {
        GamePrefsDb.incrementLaunch(context, gameDir)
    }

    fun recordPlayTime(context: Context, gameDir: String, millis: Long) {
        GamePrefsDb.addPlayTime(context, gameDir, millis)
    }

    fun getStats(context: Context, gameDir: String): GameStats {
        return GamePrefsDb.getGameStats(context, gameDir)
    }

    fun exportBackup(context: Context): File = LauncherSettingsDb.exportBackup(context)

    private fun normalizePath(value: String): String = File(value).absolutePath

    private fun escape(value: String): String {
        return value.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")
    }

    fun getLogDir(context: Context): String = NATIVE_LOG_DIR

    fun latestUnifiedLogFile(context: Context): File? {
        val dir = File(getLogDir(context))
        return dir.listFiles()
            ?.filter { it.isFile && it.name.matches(unifiedLogNameRegex) }
            ?.maxByOrNull { it.lastModified() }
    }

    fun cleanupOldUnifiedLogs(context: Context) {
        if (!getFileLogAutoCleanup(context)) return
        val dir = File(getLogDir(context))
        val cutoff = System.currentTimeMillis() -
            getFileLogRetentionDays(context).toLong() * 24L * 60L * 60L * 1000L
        dir.listFiles()
            ?.filter { it.isFile && it.name.matches(unifiedLogNameRegex) }
            ?.forEach { file ->
                if (file.lastModified() < cutoff) runCatching { file.delete() }
            }
    }

    fun clearUnifiedLogs(context: Context) {
        nativeFileLoggingConfigured = false
        File(getLogDir(context)).listFiles()
            ?.filter { it.isFile && it.name.endsWith(".log", ignoreCase = true) }
            ?.forEach { file -> runCatching { file.delete() } }
        LauncherSettingsDb.setString(context, KEY_ACTIVE_LOG_FILE, "")
    }

    fun writeLauncherLog(context: Context, message: String, throwable: Throwable? = null) {
        runCatching {
            appendUnifiedLog(context, message, throwable)
            if (throwable != null) {
                Log.w(TAG, message, throwable)
            } else {
                Log.i(TAG, message)
            }
        }.onFailure { Log.e(TAG, "Failed to write launcher log", it) }
    }

    private fun appendUnifiedLog(context: Context, message: String, throwable: Throwable? = null) {
        if (!getFileLogEnabled(context)) return
        if (nativeFileLoggingConfigured && writeNativeLauncherLog(message, throwable)) return
        val active = LauncherSettingsDb.getString(context, KEY_ACTIVE_LOG_FILE, "")
            .takeIf { it.isNotBlank() }
            ?: return
        runCatching {
            synchronized(unifiedLogLock) {
                val file = File(active)
                file.parentFile?.mkdirs()
                if (file.exists() && file.length() > 0) {
                    RandomAccessFile(file, "r").use {
                        it.seek(file.length() - 1)
                        if (it.read() != '\n'.code) {
                            file.appendText("\n")
                        }
                    }
                }
                val lineTime = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US).format(Date())
                file.appendText("[$lineTime] [launcher] $message\n")
                if (throwable != null) {
                    val sw = StringWriter()
                    throwable.printStackTrace(PrintWriter(sw))
                    file.appendText(sw.toString())
                    file.appendText("\n")
                }
            }
        }
    }

    private fun writeNativeLauncherLog(message: String, throwable: Throwable?): Boolean {
        val throwableText = throwable?.let {
            val sw = StringWriter()
            it.printStackTrace(PrintWriter(sw))
            sw.toString()
        }.orEmpty()
        return runCatching {
            org.tvp.kirikiri2.KR2Activity.nativeLauncherLog(message, throwableText)
        }.getOrDefault(false)
    }
}

data class GameStats(
    val uuid: String = "",
    val launchCount: Int = 0,
    val playTimeMillis: Long = 0L,
    val lastLaunchMillis: Long = 0L,
) {
    fun formatPlayTime(): String {
        val minutes = playTimeMillis / 60000L
        val hours = minutes / 60L
        val mins = minutes % 60L
        return if (hours > 0) "${hours}h ${mins}m" else "${mins}m"
    }
}
