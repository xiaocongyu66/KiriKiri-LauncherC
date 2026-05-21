package org.github.krkr2

import android.content.Context
import android.os.Build
import android.util.Log
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID

object LauncherPrefs {
    private const val PREF = "krkr2_launcher"
    private const val KEY_GAME_ROOT = "game_root"
    private const val KEY_LAST_GAME = "last_game"
    private const val KEY_LANGUAGE = "language"
    private const val KEY_FORCE_LANDSCAPE = "force_landscape"
    private const val KEY_KNOWN_GAMES = "known_games"
    private const val KEY_LOG_DIR = "log_dir"
    private const val TAG = "KR2LauncherPrefs"

    const val LANG_EN = "en"
    const val LANG_ZH = "zh"
    const val DEFAULT_GAME_ROOT = "/storage/emulated/0/krkr2pro"
    const val DEFAULT_LOG_DIR = "/storage/emulated/0/krkr2pro/logs"

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

    fun getLogDir(context: Context): String {
        return context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .getString(KEY_LOG_DIR, DEFAULT_LOG_DIR) ?: DEFAULT_LOG_DIR
    }

    fun setLogDir(context: Context, path: String) {
        context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_LOG_DIR, path.trim().ifBlank { DEFAULT_LOG_DIR })
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
        val knownGames = pref.getStringSet(KEY_KNOWN_GAMES, emptySet()).orEmpty().toSet() + uuid
        pref.edit()
            .putString(pathKey, uuid)
            .putString("path_$uuid", stablePath)
            .putStringSet(KEY_KNOWN_GAMES, knownGames)
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
        writeLauncherLog(context, "Set custom image for ${normalizePath(gameDir)} -> ${imagePath.trim()}")
    }

    fun setAlias(context: Context, gameDir: String, alias: String) {
        val uuid = ensureGameUuid(context, gameDir)
        context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putString("alias_$uuid", alias.trim())
            .apply()
        writeLauncherLog(context, "Set alias for ${normalizePath(gameDir)} -> ${alias.trim()}")
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
        writeLauncherLog(context, "Record launch: ${normalizePath(gameDir)}")
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
        val timestamp = timestamp()
        val outDir = File(getLogDir(context), "backups")
        outDir.mkdirs()
        val file = File(outDir, "krkr2_launcher_backup_$timestamp.json")
        val rawEntries = all.entries.joinToString(",\n") { entry ->
            "    \"${escape(entry.key)}\": \"${escape(entry.value.toString())}\""
        }
        val json = """
            {
              "exportedAt": "$timestamp",
              "settings": {
                "gameRoot": "${escape(getGameRoot(context))}",
                "language": "${escape(getLanguage(context))}",
                "forceLandscape": ${getForceLandscape(context)},
                "logDir": "${escape(getLogDir(context))}"
              },
              "rawPreferences": {
            $rawEntries
              }
            }
        """.trimIndent() + "\n"
        file.writeText(json)
        writeLauncherLog(context, "Export backup: ${file.absolutePath}")
        return file
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

    fun exportLogs(context: Context): File {
        val baseDir = File(getLogDir(context))
        baseDir.mkdirs()
        val outDir = File(baseDir, "krkr2_logs_${timestamp()}")
        outDir.mkdirs()

        val launcherLog = File(baseDir, "krkr2_launcher.log")
        if (launcherLog.exists()) {
            launcherLog.copyTo(File(outDir, launcherLog.name), overwrite = true)
        }

        val dumpDir = File(context.filesDir, "dump")
        if (dumpDir.exists()) {
            copyRecursivelySafe(dumpDir, File(outDir, "dump"))
        }

        File(outDir, "system_info.txt").writeText(
            "package=${context.packageName}\n" +
                "appFilesDir=${context.filesDir.absolutePath}\n" +
                "device=${Build.MANUFACTURER} ${Build.MODEL}\n" +
                "sdk=${Build.VERSION.SDK_INT}\n" +
                "android=${Build.VERSION.RELEASE}\n" +
                "gameRoot=${getGameRoot(context)}\n" +
                "logDir=${getLogDir(context)}\n"
        )

        val logcatNote = runCatching {
            val logcatFile = File(outDir, "logcat.txt")
            val process = Runtime.getRuntime().exec(arrayOf("logcat", "-d", "-t", "2000"))
            val stdout = process.inputStream.bufferedReader().readText()
            val stderr = process.errorStream.bufferedReader().readText()
            val exit = process.waitFor()
            logcatFile.writeText(
                "exit=$exit\n" +
                    (if (stderr.isNotBlank()) "stderr:\n$stderr\n\n" else "") +
                    "stdout:\n$stdout"
            )
            "logcat=${logcatFile.absolutePath}\n"
        }.getOrElse { error ->
            File(outDir, "logcat_error.txt").writeText(error.stackTraceToString())
            "logcat=unavailable: ${error.message.orEmpty()}\n"
        }

        File(outDir, "README.txt").writeText(
            "KrKr2 exported logs\n" +
                "time=${timestamp()}\n" +
                "launcherLog=${launcherLog.absolutePath}\n" +
                "nativeDumpDir=${dumpDir.absolutePath}\n" +
                logcatNote +
                "note=Logcat visibility is limited by Android permissions; on modern Android it usually contains this app's own log lines plus accessible system snippets.\n"
        )
        writeLauncherLog(context, "Export logs: ${outDir.absolutePath}")
        return outDir
    }

    private fun copyRecursivelySafe(source: File, target: File) {
        if (source.isDirectory) {
            target.mkdirs()
            source.listFiles().orEmpty().forEach { child ->
                copyRecursivelySafe(child, File(target, child.name))
            }
        } else if (source.isFile) {
            target.parentFile?.mkdirs()
            source.copyTo(target, overwrite = true)
        }
    }

    private fun statsKey(context: Context, gameDir: String): String {
        val normalized = normalizePath(gameDir)
        val uuid = ensureGameUuid(context, normalized)
        return "${keyOf(normalized)}_$uuid"
    }

    private fun normalizePath(value: String): String = File(value).absolutePath

    private fun keyOf(value: String): String {
        return value.lowercase(Locale.ROOT).replace(Regex("[^a-z0-9]+"), "_").trim('_').ifBlank { value.hashCode().toString(16) }
    }

    private fun escape(value: String): String {
        return value.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")
    }

    private fun timestamp(): String = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
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
