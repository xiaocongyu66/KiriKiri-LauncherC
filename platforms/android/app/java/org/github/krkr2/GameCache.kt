package org.github.krkr2

import android.content.Context
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Persistent disk cache for the launcher's discovered game list.
 *
 * The launcher used to call [GameScanner.scan] on every cold start which
 * walks the entire game root from scratch. With many archives that walk
 * blocks the UI for several hundred ms — long enough to show the user a
 * blank loader. The cache lets us repaint the last-known list at startup
 * (zero IO on the main path) and reconcile the disk state on a worker
 * thread.
 *
 * The on-disk format is a JSON array stored under SharedPreferences. We
 * intentionally keep it small (no images, just paths + lightweight
 * metadata) so reads from main thread stay sub-millisecond.
 */
object GameCache {
    private const val TAG = "KR2GameCache"
    private const val PREF = "krkr2_launcher_cache"
    private const val KEY_ENTRIES = "entries_v1"
    private const val KEY_GAME_ROOT = "cache_game_root"

    /**
     * Read the persisted entries. Returns an empty list if the cache has
     * never been written or if the JSON has gone bad — callers should
     * treat that as "no cache, do a full scan".
     */
    fun load(context: Context): List<GameEntry> {
        val pref = context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
        val raw = pref.getString(KEY_ENTRIES, null) ?: return emptyList()
        return runCatching {
            val arr = JSONArray(raw)
            (0 until arr.length()).map { i ->
                val o = arr.getJSONObject(i)
                GameEntry(
                    title = o.optString("title"),
                    gameDir = o.optString("gameDir"),
                    coverPath = o.optString("coverPath").ifBlank { null },
                    backgroundPath = o.optString("backgroundPath").ifBlank { null },
                    launchFile = o.optString("launchFile").ifBlank { null },
                    lastModified = o.optLong("lastModified", 0L),
                )
            }
        }.getOrElse {
            Log.w(TAG, "Cache parse failed, dropping cache", it)
            clear(context)
            emptyList()
        }
    }

    /**
     * Persist the entry list. We sort by lastModified desc to match the
     * scanner output so subsequent reads paint in the same order without
     * an extra sort pass.
     */
    fun save(context: Context, entries: List<GameEntry>) {
        val arr = JSONArray()
        entries.forEach { e ->
            val o = JSONObject()
            o.put("title", e.title)
            o.put("gameDir", e.gameDir)
            o.put("coverPath", e.coverPath ?: "")
            o.put("backgroundPath", e.backgroundPath ?: "")
            o.put("launchFile", e.launchFile ?: "")
            o.put("lastModified", e.lastModified)
            arr.put(o)
        }
        context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_ENTRIES, arr.toString())
            .apply()
    }

    fun getCachedRoot(context: Context): String? {
        return context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .getString(KEY_GAME_ROOT, null)
    }

    fun setCachedRoot(context: Context, root: String) {
        context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_GAME_ROOT, root)
            .apply()
    }

    fun clear(context: Context) {
        context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .clear()
            .apply()
    }

    /**
     * Decide whether a cached entry is still valid. Used by the background
     * verifier to prune entries whose backing directories have been deleted
     * or emptied. Returns:
     *   - VALID:    directory exists and has at least one file
     *   - GONE:     directory does not exist anymore
     *   - EMPTY:    directory exists but contains no files (recursively
     *               looking only one level for speed)
     */
    enum class EntryStatus { VALID, GONE, EMPTY }

    fun checkEntry(entry: GameEntry): EntryStatus {
        val dir = File(entry.gameDir)
        if (!dir.exists() || !dir.isDirectory) return EntryStatus.GONE
        val children = runCatching { dir.listFiles()?.toList().orEmpty() }
            .getOrDefault(emptyList())
        if (children.isEmpty()) return EntryStatus.EMPTY
        // Treat "directory containing only sub-directories that are all
        // empty" as empty too — covers the case where the user manually
        // deleted xp3s but left the folder shells behind.
        val hasAnyFile = children.any { it.isFile } || children.any { sub ->
            sub.isDirectory && runCatching {
                sub.listFiles()?.any { it.isFile } ?: false
            }.getOrDefault(false)
        }
        return if (hasAnyFile) EntryStatus.VALID else EntryStatus.EMPTY
    }
}
