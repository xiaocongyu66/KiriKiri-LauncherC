package org.github.krkr2

import android.content.Context
import android.util.Log
import android.util.Xml
import org.xmlpull.v1.XmlPullParser
import java.io.File
import java.io.FileWriter
import java.io.StringReader

/**
 * Read/write the engine's `GlobalPreference.xml` directly.
 *
 * Format (see GlobalConfigManager.cpp::SaveToFile):
 * ```
 * <?xml version="1.0"?>
 * <GlobalPreference>
 *   <Item key="..." value="..."/>
 *   <Item key="..." value="..."/>
 *   <Custom key="..." value="..."/>     <!-- preserved as-is -->
 *   <KeyMap key="..." value="..."/>     <!-- preserved as-is -->
 * </GlobalPreference>
 * ```
 *
 * File location: `<filesDir>/.preference/GlobalPreference.xml`. The launcher
 * runs in the same process UID as SdlRuntimeActivity, so plain `File` reads/writes
 * work — no JNI required.
 *
 * Concurrency note: writes are not atomic. The engine and launcher should
 * not run concurrently (launcher activity finishes before SdlRuntimeActivity
 * starts), so the worst case is "user changes a setting, engine doesn't see
 * it until next launch", which is the same semantics as the engine's own
 * SaveToFile flow.
 */
object KrkrPrefsStore {
    private const val TAG = "KrkrPrefsStore"
    private const val PREF_DIR = ".preference"
    private const val PREF_FILE = "GlobalPreference.xml"

    /** Resolve the engine config file path. Creates the dir on demand. */
    fun configFile(context: Context): File {
        val dir = File(context.filesDir, PREF_DIR)
        if (!dir.exists()) dir.mkdirs()
        return File(dir, PREF_FILE)
    }

    /**
     * Snapshot of everything in the engine config file. We split into three
     * buckets so the launcher can edit `items` without losing keymap or the
     * Custom command-line entries on save.
     */
    data class Snapshot(
        val items: Map<String, String>,
        val customs: List<Pair<String, String>>,
        val keyMaps: List<Pair<Int, Int>>,
    ) {
        companion object {
            val EMPTY = Snapshot(emptyMap(), emptyList(), emptyList())
        }
    }

    /** Load a snapshot. Returns [Snapshot.EMPTY] if file is absent or unreadable. */
    fun load(context: Context): Snapshot {
        val file = configFile(context)
        if (!file.exists()) return Snapshot.EMPTY
        return runCatching { parse(file.readText(Charsets.UTF_8)) }
            .onFailure { Log.w(TAG, "load failed, treating as empty", it) }
            .getOrDefault(Snapshot.EMPTY)
    }

    /** Read a single string with default. Cheap because we re-parse, but the file is tiny. */
    fun getString(context: Context, key: String, default: String): String =
        load(context).items[key] ?: default

    fun getBool(context: Context, key: String, default: Boolean): Boolean {
        val raw = load(context).items[key] ?: return default
        // C++ side persists bools as "0"/"1" via SetValueInt(name, defVal ? 1 : 0).
        return raw.trim().let { it == "1" || it.equals("true", ignoreCase = true) }
    }

    fun getFloat(context: Context, key: String, default: Float): Float =
        load(context).items[key]?.toFloatOrNull() ?: default

    /**
     * Apply [updates] over the existing snapshot and rewrite the file.
     *
     * We always rewrite the whole document — the engine does the same on
     * SaveToFile, and the file is small (a few KB). Empty values are still
     * stored as empty strings (C++ behavior preserves them).
     */
    fun update(context: Context, updates: Map<String, String>) {
        if (updates.isEmpty()) return
        val current = load(context)
        val merged = current.items.toMutableMap()
        for ((k, v) in updates) merged[k] = v
        write(context, current.copy(items = merged))
    }

    fun setBool(context: Context, key: String, value: Boolean) =
        update(context, mapOf(key to if (value) "1" else "0"))

    fun setString(context: Context, key: String, value: String) =
        update(context, mapOf(key to value))

    fun setFloat(context: Context, key: String, value: Float) =
        update(context, mapOf(key to value.toString()))

    /** Reset every key the launcher knows about to its schema default. */
    fun resetToDefaults(context: Context) {
        val defaults = KrkrPrefsSchema.ALL_BY_KEY.values.mapNotNull { item ->
            when (item) {
                is KrkrPrefsSchema.PrefItem.Bool -> item.key to (if (item.default) "1" else "0")
                is KrkrPrefsSchema.PrefItem.Select -> item.key to item.default
                is KrkrPrefsSchema.PrefItem.SliderFloat -> item.key to item.default.toString()
                is KrkrPrefsSchema.PrefItem.TextField -> item.key to item.default
                is KrkrPrefsSchema.PrefItem.Constant -> null
            }
        }.toMap()
        update(context, defaults)
    }

    // ---- XML parsing / serialization ----------------------------------------

    private fun parse(xml: String): Snapshot {
        val items = mutableMapOf<String, String>()
        val customs = mutableListOf<Pair<String, String>>()
        val keyMaps = mutableListOf<Pair<Int, Int>>()

        val parser: XmlPullParser = Xml.newPullParser().apply {
            setFeature(XmlPullParser.FEATURE_PROCESS_NAMESPACES, false)
            setInput(StringReader(xml))
        }
        var ev = parser.eventType
        while (ev != XmlPullParser.END_DOCUMENT) {
            if (ev == XmlPullParser.START_TAG) {
                val key = parser.getAttributeValue(null, "key").orEmpty()
                val value = parser.getAttributeValue(null, "value").orEmpty()
                when (parser.name) {
                    "Item" -> if (key.isNotEmpty()) items[key] = value
                    "Custom" -> if (key.isNotEmpty()) customs += key to value
                    "KeyMap" -> {
                        val k = key.toIntOrNull()
                        val v = value.toIntOrNull()
                        if (k != null && v != null) keyMaps += k to v
                    }
                }
            }
            ev = parser.next()
        }
        return Snapshot(items, customs, keyMaps)
    }

    private fun write(context: Context, snapshot: Snapshot) {
        val file = configFile(context)
        val tmp = File(file.parentFile, "${file.name}.tmp")
        try {
            FileWriter(tmp).use { w ->
                w.append("<?xml version=\"1.0\"?>\n")
                w.append("<GlobalPreference>\n")
                snapshot.items.forEach { (k, v) ->
                    w.append("  <Item key=\"").append(escape(k))
                        .append("\" value=\"").append(escape(v)).append("\"/>\n")
                }
                snapshot.customs.forEach { (k, v) ->
                    w.append("  <Custom key=\"").append(escape(k))
                        .append("\" value=\"").append(escape(v)).append("\"/>\n")
                }
                snapshot.keyMaps.forEach { (k, v) ->
                    w.append("  <KeyMap key=\"").append(k.toString())
                        .append("\" value=\"").append(v.toString()).append("\"/>\n")
                }
                w.append("</GlobalPreference>\n")
            }
            // Atomic-ish replace — on the same filesystem this is a rename
            // syscall that's safe against concurrent reads.
            if (file.exists()) file.delete()
            tmp.renameTo(file)
        } catch (t: Throwable) {
            Log.e(TAG, "write failed: ${t.message}", t)
            tmp.delete()
        }
    }

    private fun escape(value: String): String {
        // Manual escape; entity replacements must be in this exact order so
        // that `&` doesn't double-encode. We avoid `"\""` literal because
        // the file format limits in our toolchain choke on raw triple quotes.
        val sb = StringBuilder(value.length)
        for (ch in value) {
            when (ch) {
                '&' -> sb.append("&amp;")
                '<' -> sb.append("&lt;")
                '>' -> sb.append("&gt;")
                '\u0022' -> { sb.append('&'); sb.append('q'); sb.append('u'); sb.append('o'); sb.append('t'); sb.append(';') }  // double quote -> "
                else -> sb.append(ch)
            }
        }
        return sb.toString()
    }
}
