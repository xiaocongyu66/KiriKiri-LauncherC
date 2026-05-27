package org.github.krkr2
import android.content.Context
import android.util.Log
import java.io.File
import java.io.PrintWriter
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

    fun getLogDir(context: Context): String {
        // External app-scoped logs dir so the user can pull the file without root.
        val ext = context.getExternalFilesDir(null)
        return (ext ?: context.filesDir).absolutePath
    }

    fun writeLauncherLog(context: Context, message: String, throwable: Throwable? = null) {
        runCatching {
            val logDir = File(getLogDir(context))
            logDir.mkdirs()
            val logFile = File(logDir, "krkr2_launcher.log")
            val lineTime = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US).format(Date())
            logFile.appendText("[$lineTime] $message\n")
            if (throwable != null) {
                val sw = StringWriter()
                throwable.printStackTrace(PrintWriter(sw))
                logFile.appendText(sw.toString())
                logFile.appendText("\n")
            }
        }.onFailure { Log.e(TAG, "Failed to write launcher log", it) }
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