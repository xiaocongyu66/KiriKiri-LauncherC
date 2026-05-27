package org.github.krkr2

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/**
 * SQLite store for launcher-level settings only.
 * Game-level settings live in [GamePrefsDb].
 */
object LauncherSettingsDb {
    private const val DB_NAME = "krkr2_launcher_settings.db"
    private const val DB_VERSION = 1
    private const val TABLE = "launcher_settings"
    private const val COL_KEY = "setting_key"
    private const val COL_VALUE = "setting_value"

    private class Helper(context: Context) : SQLiteOpenHelper(context, DB_NAME, null, DB_VERSION) {
        override fun onCreate(db: SQLiteDatabase) {
            db.execSQL(
                """
                CREATE TABLE IF NOT EXISTS $TABLE (
                    $COL_KEY TEXT PRIMARY KEY,
                    $COL_VALUE TEXT NOT NULL
                )
                """.trimIndent()
            )
        }

        override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
            if (oldVersion < 1) onCreate(db)
        }
    }

    private fun db(context: Context): SQLiteDatabase = Helper(context.applicationContext).writableDatabase

    fun getString(context: Context, key: String, default: String): String {
        db(context).use { readable ->
            readable.rawQuery("SELECT $COL_VALUE FROM $TABLE WHERE $COL_KEY = ?", arrayOf(key)).use { cursor ->
                return if (cursor.moveToFirst()) cursor.getString(0) else default
            }
        }
    }

    fun getInt(context: Context, key: String, default: Int): Int = getString(context, key, default.toString()).toIntOrNull() ?: default

    fun getBoolean(context: Context, key: String, default: Boolean): Boolean {
        val raw = getString(context, key, if (default) "1" else "0")
        return raw.trim().let { it == "1" || it.equals("true", ignoreCase = true) }
    }

    fun setString(context: Context, key: String, value: String) = putRaw(context, key, value)
    fun setInt(context: Context, key: String, value: Int) = putRaw(context, key, value.toString())
    fun setBoolean(context: Context, key: String, value: Boolean) = putRaw(context, key, if (value) "1" else "0")

    fun remove(context: Context, key: String) {
        db(context).use { writable ->
            writable.delete(TABLE, "$COL_KEY = ?", arrayOf(key))
        }
    }

    fun exportBackup(context: Context): File {
        val dbFile = context.getDatabasePath(DB_NAME)
        val outDir = File(context.getExternalFilesDir(null) ?: context.filesDir, "backups")
        if (!outDir.exists()) outDir.mkdirs()
        val target = File(outDir, "krkr2_launcher_settings_backup_${System.currentTimeMillis()}.db")
        if (dbFile.exists()) {
            Files.copy(dbFile.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING)
        } else {
            target.writeBytes(byteArrayOf())
        }
        return target
    }

    private fun putRaw(context: Context, key: String, value: String) {
        db(context).use { writable ->
            val row = ContentValues().apply {
                put(COL_KEY, key)
                put(COL_VALUE, value)
            }
            writable.insertWithOnConflict(TABLE, null, row, SQLiteDatabase.CONFLICT_REPLACE)
        }
    }
}
