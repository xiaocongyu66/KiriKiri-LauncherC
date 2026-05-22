package org.github.krkr2
import android.content.Context
import android.util.Log
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID
object LauncherPrefs {
    private const val TAG = "KR2LauncherPrefs"
    private const val PREF = "krkr2_launcher"
    private const val KEY_GAME_ROOT = "game_root"
    private const val KEY_LAST_GAME = "last_game"
    private const val KEY_LANGUAGE = "language"
    private const val KEY_FORCE_LANDSCAPE = "force_landscape"
    private const val KEY_KNOWN_GAMES = "known_games"

    const val LANG_EN = "en"
    const val LANG_ZH = "zh"
    const val DEFAULT_GAME_ROOT = "/storage/emulated/0/krkr2pro"

    fun getGameRoot(context: Context): String {
        return context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .getString(KEY_GAME_ROOT, DEFAULT_GAME_ROOT) ?: DEFAULT_GAME_ROOT
    }

    fun setGameRoot(context: Context, path: String) {
        context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_GAME_ROOT, path.trim().ifBlank { DEFAULT_GAME_ROOT })
            .apply()
    }

    fun getLanguage(context: Context): String {
        return context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .getString(KEY_LANGUAGE, LANG_EN) ?: LANG_EN
    }

    fun setLanguage(context: Context, language: String) {
        context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_LANGUAGE, if (language == LANG_ZH) LANG_ZH else LANG_EN)
            .apply()
    }

    fun getForceLandscape(context: Context): Boolean {
        return context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .getBoolean(KEY_FORCE_LANDSCAPE, true)
    }

    fun setForceLandscape(context: Context, enabled: Boolean) {
        context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_FORCE_LANDSCAPE, enabled)
            .apply()
    }

    fun getLastGamePath(context: Context): String? {
        return context.getSharedPreferences(PREF, Context.MODE_PRIVATE).getString(KEY_LAST_GAME, null)
    }

    fun setLastGamePath(context: Context, path: String) {
        context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_LAST_GAME, path)
            .apply()
    }

    fun ensureGameUuid(context: Context, gameDir: String): String {
        val stablePath = normalizePath(gameDir)
        val pref = context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
        val pathKey = "uuid_${keyOf(stablePath)}"
        val existing = pref.getString(pathKey, null)
        if (!existing.isNullOrBlank()) return existing
        val uuid = UUID.randomUUID().toString()
        pref.edit()
            .putString(pathKey, uuid)
            .putString("path_$uuid", stablePath)
            .putStringSet(KEY_KNOWN_GAMES, pref.getStringSet(KEY_KNOWN_GAMES, emptySet()).orEmpty() + uuid)
            .apply()
        return uuid
    }

    fun getGameUuid(context: Context, gameDir: String): String = ensureGameUuid(context, gameDir)

    fun getAlias(context: Context, gameDir: String): String? {
        val uuid = ensureGameUuid(context, gameDir)
        return context.getSharedPreferences(PREF, Context.MODE_PRIVATE).getString("alias_$uuid", null)
    }

    fun getCustomImagePath(context: Context, gameDir: String): String? {
        val uuid = ensureGameUuid(context, gameDir)
        return context.getSharedPreferences(PREF, Context.MODE_PRIVATE).getString("custom_image_$uuid", null)
    }

    fun setCustomImagePath(context: Context, gameDir: String, imagePath: String) {
        val uuid = ensureGameUuid(context, gameDir)
        context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putString("custom_image_$uuid", imagePath.trim())
            .apply()
    }

    fun setAlias(context: Context, gameDir: String, alias: String) {
        val uuid = ensureGameUuid(context, gameDir)
        context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putString("alias_$uuid", alias.trim())
            .apply()
    }

    fun displayName(context: Context, game: GameEntry): String {
        return getAlias(context, game.gameDir)?.takeIf { it.isNotBlank() } ?: game.title
    }

    fun recordLaunch(context: Context, gameDir: String) {
        val key = statsKey(context, gameDir)
        val pref = context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
        ensureGameUuid(context, gameDir)
        pref.edit()
            .putInt("launch_count_$key", pref.getInt("launch_count_$key", 0) + 1)
            .putLong("last_launch_$key", System.currentTimeMillis())
            .apply()
    }

    fun recordPlayTime(context: Context, gameDir: String, millis: Long) {
        if (millis <= 0L) return
        val key = statsKey(context, gameDir)
        val pref = context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
        ensureGameUuid(context, gameDir)
        pref.edit()
            .putLong("play_time_$key", pref.getLong("play_time_$key", 0L) + millis)
            .apply()
    }

    fun getStats(context: Context, gameDir: String): GameStats {
        val key = statsKey(context, gameDir)
        val pref = context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
        return GameStats(
            uuid = ensureGameUuid(context, gameDir),
            launchCount = pref.getInt("launch_count_$key", 0),
            playTimeMillis = pref.getLong("play_time_$key", 0L),
            lastLaunchMillis = pref.getLong("last_launch_$key", 0L),
        )
    }

    fun exportBackup(context: Context): File {
        val pref = context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
        val all = pref.all
        val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val outDir = File(DEFAULT_GAME_ROOT, "backups")
        outDir.mkdirs()
        val file = File(outDir, "krkr2_launcher_backup_$timestamp.json")
        val json = buildString {
            append("{\n")
            append("  \"exportedAt\": \"").append(timestamp).append("\",\n")
            append("  \"settings\": {\n")
            append("    \"gameRoot\": \"").append(escape(getGameRoot(context))).append("\",\n")
            append("    \"language\": \"").append(escape(getLanguage(context))).append("\",\n")
            append("    \"forceLandscape\": ").append(getForceLandscape(context)).append("\n")
            append("  },\n")
            append("  \"rawPreferences\": {\n")
            all.entries.forEachIndexed { index, entry ->
                append("    \"").append(escape(entry.key)).append("\": \"").append(escape(entry.value.toString())).append("\"")
                if (index != all.size - 1) append(",")
                append("\n")
            }
            append("  }\n")
            append("}\n")
        }
        file.writeText(json)
        return file
    }

    private fun statsKey(context: Context, gameDir: String): String {
        val normalized = normalizePath(gameDir)
        val uuid = ensureGameUuid(context, normalized)
        return "${keyOf(normalized)}_$uuid"
    }

    private fun normalizePath(value: String): String = File(value).absolutePath

    private fun keyOf(value: String): String {
        return value.lowercase(Locale.ROOT).replace(Regex("[^a-z0-9]+"), "_").trim('_')
    }

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