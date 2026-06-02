package org.github.krkr2

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import java.io.File
import java.util.UUID

/**
 * SQLite-backed per-game metadata and preference store.
 *
 * We keep global launcher prefs in SharedPreferences / GlobalPreference.xml,
 * but every game-specific value lives here so UUID mapping and per-game
 * overrides do not leak into the launcher cache layer.
 */
object GamePrefsDb {
    private const val DB_NAME = "krkr2_game_prefs.db"
    private const val DB_VERSION = 1

    private const val TABLE_GAMES = "games"
    private const val TABLE_KV = "game_kv"
    private const val TABLE_STATS = "game_stats"

    private const val COL_UUID = "uuid"
    private const val COL_STABLE_PATH = "stable_path"
    private const val COL_CREATED_AT = "created_at"
    private const val COL_UPDATED_AT = "updated_at"

    private const val COL_KEY = "pref_key"
    private const val COL_VALUE = "pref_value"

    private const val COL_LAUNCH_COUNT = "launch_count"
    private const val COL_PLAY_TIME_MS = "play_time_ms"
    private const val COL_LAST_LAUNCH_MS = "last_launch_ms"

    private class Helper(context: Context) : SQLiteOpenHelper(context, DB_NAME, null, DB_VERSION) {
        override fun onConfigure(db: SQLiteDatabase) {
            db.setForeignKeyConstraintsEnabled(true)
        }

        override fun onCreate(db: SQLiteDatabase) {
            db.execSQL(
                """
                CREATE TABLE IF NOT EXISTS $TABLE_GAMES (
                    $COL_UUID TEXT PRIMARY KEY,
                    $COL_STABLE_PATH TEXT NOT NULL UNIQUE,
                    $COL_CREATED_AT INTEGER NOT NULL,
                    $COL_UPDATED_AT INTEGER NOT NULL
                )
                """.trimIndent()
            )
            db.execSQL(
                """
                CREATE TABLE IF NOT EXISTS $TABLE_KV (
                    $COL_UUID TEXT NOT NULL,
                    $COL_KEY TEXT NOT NULL,
                    $COL_VALUE TEXT,
                    PRIMARY KEY ($COL_UUID, $COL_KEY),
                    FOREIGN KEY ($COL_UUID) REFERENCES $TABLE_GAMES($COL_UUID) ON DELETE CASCADE
                )
                """.trimIndent()
            )
            db.execSQL(
                """
                CREATE TABLE IF NOT EXISTS $TABLE_STATS (
                    $COL_UUID TEXT PRIMARY KEY,
                    $COL_LAUNCH_COUNT INTEGER NOT NULL DEFAULT 0,
                    $COL_PLAY_TIME_MS INTEGER NOT NULL DEFAULT 0,
                    $COL_LAST_LAUNCH_MS INTEGER NOT NULL DEFAULT 0,
                    FOREIGN KEY ($COL_UUID) REFERENCES $TABLE_GAMES($COL_UUID) ON DELETE CASCADE
                )
                """.trimIndent()
            )
        }

        override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
            if (oldVersion < 1) onCreate(db)
        }
    }

    private fun db(context: Context): SQLiteDatabase = Helper(context.applicationContext).writableDatabase

    fun ensureGameUuid(context: Context, gameDir: String): String {
        val stablePath = normalizePath(gameDir)
        db(context).use { writable ->
            writable.beginTransaction()
            try {
                writable.rawQuery(
                    "SELECT $COL_UUID FROM $TABLE_GAMES WHERE $COL_STABLE_PATH = ?",
                    arrayOf(stablePath)
                ).use { cursor ->
                    if (cursor.moveToFirst()) {
                        val existing = cursor.getString(0)
                        writable.execSQL(
                            "UPDATE $TABLE_GAMES SET $COL_UPDATED_AT = ? WHERE $COL_UUID = ?",
                            arrayOf(System.currentTimeMillis(), existing)
                        )
                        writable.setTransactionSuccessful()
                        return existing
                    }
                }

                val uuid = UUID.randomUUID().toString()
                val now = System.currentTimeMillis()
                val gameRow = ContentValues().apply {
                    put(COL_UUID, uuid)
                    put(COL_STABLE_PATH, stablePath)
                    put(COL_CREATED_AT, now)
                    put(COL_UPDATED_AT, now)
                }
                writable.insertOrThrow(TABLE_GAMES, null, gameRow)
                val statRow = ContentValues().apply { put(COL_UUID, uuid) }
                writable.insertWithOnConflict(TABLE_STATS, null, statRow, SQLiteDatabase.CONFLICT_IGNORE)
                writable.setTransactionSuccessful()
                return uuid
            } finally {
                writable.endTransaction()
            }
        }
    }

    fun getGamePref(context: Context, gameDir: String, key: String): String? {
        val uuid = ensureGameUuid(context, gameDir)
        db(context).use { readable ->
            readable.rawQuery(
                "SELECT $COL_VALUE FROM $TABLE_KV WHERE $COL_UUID = ? AND $COL_KEY = ?",
                arrayOf(uuid, key)
            ).use { cursor ->
                return if (cursor.moveToFirst()) cursor.getString(0) else null
            }
        }
    }

    fun putGamePref(context: Context, gameDir: String, key: String, value: String?) {
        val uuid = ensureGameUuid(context, gameDir)
        db(context).use { writable ->
            if (value == null) {
                writable.delete(
                    TABLE_KV,
                    "$COL_UUID = ? AND $COL_KEY = ?",
                    arrayOf(uuid, key)
                )
                return
            }
            val row = ContentValues().apply {
                put(COL_UUID, uuid)
                put(COL_KEY, key)
                put(COL_VALUE, value)
            }
            writable.insertWithOnConflict(TABLE_KV, null, row, SQLiteDatabase.CONFLICT_REPLACE)
        }
    }

    fun getGameStats(context: Context, gameDir: String): GameStats {
        val uuid = ensureGameUuid(context, gameDir)
        db(context).use { readable ->
            readable.rawQuery(
                "SELECT $COL_LAUNCH_COUNT, $COL_PLAY_TIME_MS, $COL_LAST_LAUNCH_MS FROM $TABLE_STATS WHERE $COL_UUID = ?",
                arrayOf(uuid)
            ).use { cursor ->
                if (cursor.moveToFirst()) {
                    return GameStats(
                        uuid = uuid,
                        launchCount = cursor.getInt(0),
                        playTimeMillis = cursor.getLong(1),
                        lastLaunchMillis = cursor.getLong(2),
                    )
                }
            }
        }
        return GameStats(uuid = uuid)
    }

    fun incrementLaunch(context: Context, gameDir: String) {
        val uuid = ensureGameUuid(context, gameDir)
        db(context).use { writable ->
            writable.execSQL(
                """
                INSERT INTO $TABLE_STATS ($COL_UUID, $COL_LAUNCH_COUNT, $COL_PLAY_TIME_MS, $COL_LAST_LAUNCH_MS)
                VALUES (?, 1, 0, ?)
                ON CONFLICT($COL_UUID) DO UPDATE SET
                    $COL_LAUNCH_COUNT = $COL_LAUNCH_COUNT + 1,
                    $COL_LAST_LAUNCH_MS = excluded.$COL_LAST_LAUNCH_MS
                """.trimIndent(),
                arrayOf(uuid, System.currentTimeMillis())
            )
        }
    }

    fun addPlayTime(context: Context, gameDir: String, millis: Long) {
        if (millis <= 0L) return
        val uuid = ensureGameUuid(context, gameDir)
        db(context).use { writable ->
            writable.execSQL(
                """
                INSERT INTO $TABLE_STATS ($COL_UUID, $COL_LAUNCH_COUNT, $COL_PLAY_TIME_MS, $COL_LAST_LAUNCH_MS)
                VALUES (?, 0, ?, 0)
                ON CONFLICT($COL_UUID) DO UPDATE SET
                    $COL_PLAY_TIME_MS = $COL_PLAY_TIME_MS + excluded.$COL_PLAY_TIME_MS
                """.trimIndent(),
                arrayOf(uuid, millis)
            )
        }
    }

    fun pruneMissingGames(context: Context): Int {
        db(context).use { writable ->
            val missing = mutableListOf<String>()
            writable.rawQuery(
                "SELECT $COL_UUID, $COL_STABLE_PATH FROM $TABLE_GAMES",
                emptyArray()
            ).use { cursor ->
                while (cursor.moveToNext()) {
                    val uuid = cursor.getString(0)
                    val stablePath = cursor.getString(1)
                    if (!File(stablePath).exists()) missing += uuid
                }
            }
            if (missing.isEmpty()) return 0

            writable.beginTransaction()
            try {
                missing.forEach { uuid ->
                    val where = "$COL_UUID = ?"
                    val args = arrayOf(uuid)
                    writable.delete(TABLE_KV, where, args)
                    writable.delete(TABLE_STATS, where, args)
                    writable.delete(TABLE_GAMES, where, args)
                }
                writable.setTransactionSuccessful()
            } finally {
                writable.endTransaction()
            }
            return missing.size
        }
    }

    private fun normalizePath(value: String): String = java.io.File(value).absolutePath
}
