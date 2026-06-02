package org.github.krkr2

import android.util.Log
import java.io.File
import java.util.Locale

data class GameEntry(
    val title: String,
    val gameDir: String,
    val coverPath: String? = null,
    val backgroundPath: String? = null,
    val launchFile: String? = null,
    val lastModified: Long = 0L,
) {
    val iconPath: String? get() = coverPath
    val bannerPath: String? get() = backgroundPath
    val thumbnailPath: String? get() = coverPath
    val description: String? get() = null
}

object GameScanner {
    private const val TAG = "KR2GameScanner"
    private val gameMarkers = setOf(
        "startup.tjs", "start.tjs", "data.xp3", "patch.xp3", "scenario.ks", "first.ks", "config.tjs"
    )
    private val imageExts = setOf("jpg", "jpeg", "png", "webp")
    private val coverNames = listOf(
        "cover", "icon", "title", "thumb", "thumbnail", "package", "bg", "background", "main"
    )
    private val launchExts = setOf("xp3", "tjs", "ks")
    private val preferredLaunchNames = listOf(
        "startup.tjs", "start.tjs", "data.xp3", "startup.xp3", "start.xp3",
        "main.xp3", "game.xp3", "first.ks", "scenario.ks",
    )
    private val nativeAvailable: Boolean = runCatching {
        System.loadLibrary("krkr2")
        true
    }.getOrElse {
        Log.w(TAG, "Native scanner unavailable, using Kotlin scanner", it)
        false
    }

    private fun interface NativeProgress {
        fun onPath(path: String)
    }

    private external fun nativeScan(rootPath: String, maxDepth: Int, progress: NativeProgress?): Array<GameEntry>?
    private external fun nativeListLaunchCandidates(rootPath: String): Array<String>?

    /**
     * Scan [root] for kirikiri/KAG game directories.
     *
     * @param maxDepth maximum directory recursion below [root]. The
     * default of 2 matches Tyranor's behavior — kirikiri games typically
     * live at <root>/<game>/, so depth 2 already covers a publisher folder
     * wrapping individual titles. Configurable 1..10 via Settings.
     * @param onProgress optional callback invoked once per directory the
     * scanner enters. The current directory's absolute path is passed so
     * the caller can show "Scanning /storage/.../foo" in the loading UI
     * without having to poll. Called on the calling thread.
     */
    fun scan(
        root: File,
        maxDepth: Int = 2,
        onProgress: ((String) -> Unit)? = null,
    ): List<GameEntry> {
        if (!root.exists() || !root.isDirectory) return emptyList()
        val clampedDepth = maxDepth.coerceIn(0, 32)
        if (nativeAvailable) {
            val native = runCatching {
                nativeScan(
                    root.absolutePath,
                    clampedDepth,
                    onProgress?.let { callback -> NativeProgress { path -> callback(path) } },
                )?.toList()
            }.getOrElse {
                Log.w(TAG, "Native scan failed, using Kotlin scanner", it)
                null
            }
            if (native != null) return sortEntries(native)
        }
        return scanKotlinFallback(root, clampedDepth, onProgress)
    }

    private fun scanKotlinFallback(
        root: File,
        maxDepth: Int,
        onProgress: ((String) -> Unit)?,
    ): List<GameEntry> {
        val result = linkedMapOf<String, GameEntry>()
        scanDir(root, 0, maxDepth, result, onProgress)
        return sortEntries(result.values)
    }

    private fun sortEntries(entries: Collection<GameEntry>): List<GameEntry> =
        entries.sortedWith(compareByDescending<GameEntry> { it.lastModified }.thenBy { it.title.lowercase(Locale.ROOT) })

    private fun scanDir(
        dir: File,
        depth: Int,
        maxDepth: Int,
        result: MutableMap<String, GameEntry>,
        onProgress: ((String) -> Unit)?,
    ) {
        if (depth > maxDepth || dir.name.startsWith(".")) return
        if (!dir.canRead()) return
        onProgress?.invoke(dir.absolutePath)
        val children = runCatching { dir.listFiles()?.toList().orEmpty() }.getOrDefault(emptyList())
        if (children.isEmpty()) return

        if (isGameDir(dir, children)) {
            val entry = buildGameEntry(dir, children)
            result[entry.gameDir] = entry
            // Tyranor-style pruning: once we identify a game directory we
            // do NOT descend further. Kirikiri games never nest inside
            // each other, so descending only wastes IO walking the game's
            // own asset tree (image/, voice/, scenario/, ...).
            return
        }

        children.asSequence()
            .filter { it.isDirectory && it.canRead() }
            .filterNot { it.name.startsWith(".") }
            .forEach { scanDir(it, depth + 1, maxDepth, result, onProgress) }
    }

    private fun isGameDir(dir: File, children: List<File>): Boolean {
        val names = children.map { it.name.lowercase(Locale.ROOT) }.toSet()
        if (gameMarkers.any { it in names }) return true
        if (children.any { it.isFile && it.extension.equals("xp3", true) }) return true
        if (children.any { it.isFile && it.extension.equals("ks", true) }) return true
        return File(dir, "data").isDirectory && File(dir, "scenario").isDirectory
    }

    private fun buildGameEntry(dir: File, children: List<File>): GameEntry {
        val title = readTitle(dir, children)
        val images = collectImages(dir, children)
        val cover = chooseCover(images)
        val bg = chooseBackground(images)
        val launch = chooseLaunchFile(children)
        val latest = children.maxOfOrNull { it.lastModified() } ?: dir.lastModified()
        return GameEntry(
            title = title,
            gameDir = dir.absolutePath,
            coverPath = cover?.absolutePath,
            backgroundPath = bg?.absolutePath,
            launchFile = launch?.absolutePath,
            lastModified = latest,
        )
    }

    private fun readTitle(dir: File, children: List<File>): String {
        val titleFile = children.firstOrNull { it.name.equals("title.txt", true) || it.name.equals("game.txt", true) }
        val fromFile = titleFile?.runCatchingReadText()?.lineSequence()?.firstOrNull { it.isNotBlank() }?.trim()
        if (!fromFile.isNullOrBlank()) return fromFile
        val info = children.firstOrNull { it.name.equals("package.json", true) || it.name.equals("info.json", true) }
        val json = info?.runCatchingReadText()
        val nameMatch = Regex("\\\"(?:title|name)\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"").find(json.orEmpty())?.groupValues?.getOrNull(1)
        if (!nameMatch.isNullOrBlank()) return nameMatch
        return dir.name.replace('_', ' ').replace('-', ' ').trim().ifBlank { dir.absolutePath }
    }

    private fun collectImages(dir: File, children: List<File>): List<File> {
        val direct = children.filter { it.isFile && it.extension.lowercase(Locale.ROOT) in imageExts }
        val commonFolders = listOf("image", "images", "bg", "bgimage", "background", "system", "title", "ui")
        val nested = commonFolders.flatMap { folder ->
            val f = File(dir, folder)
            runCatching { f.listFiles()?.filter { it.isFile && it.extension.lowercase(Locale.ROOT) in imageExts }.orEmpty() }.getOrDefault(emptyList())
        }
        return (direct + nested).distinctBy { it.absolutePath }
    }

    private fun chooseCover(images: List<File>): File? {
        if (images.isEmpty()) return null
        val named = images.firstOrNull { file ->
            val base = file.nameWithoutExtension.lowercase(Locale.ROOT)
            coverNames.any { base == it || base.contains(it) }
        }
        return named ?: images.maxByOrNull { it.length() }
    }

    private fun chooseBackground(images: List<File>): File? {
        if (images.isEmpty()) return null
        return images.firstOrNull {
            val n = it.nameWithoutExtension.lowercase(Locale.ROOT)
            n.contains("bg") || n.contains("back") || n.contains("title")
        } ?: images.maxByOrNull { it.length() }
    }

    private fun chooseLaunchFile(children: List<File>): File? {
        return children.asSequence()
            .filter { it.isFile && it.extension.lowercase(Locale.ROOT) in launchExts }
            .minWithOrNull(compareBy<File> { launchRank(it) }.thenBy { it.name.lowercase(Locale.ROOT) })
    }

    /**
     * Enumerate every plausible launch entry under [dir]. Used by the
     * per-game settings sheet so the user can pick a non-default boot file
     * (e.g. `startup.xp3`, `初始化.xp3`, a hand-written `.tjs`). Sorted
     * with engine-canonical names first, then likely boot archives/scripts,
     * then asset archives, so the obvious entry sits at the top.
     */
    fun listLaunchCandidates(dir: File): List<File> {
        if (!dir.exists() || !dir.isDirectory) return emptyList()
        if (nativeAvailable) {
            val native = runCatching {
                nativeListLaunchCandidates(dir.absolutePath)?.map(::File)
            }.getOrElse {
                Log.w(TAG, "Native launch candidate scan failed, using Kotlin scanner", it)
                null
            }
            if (native != null) return native
        }
        return listLaunchCandidatesKotlin(dir)
    }

    private fun listLaunchCandidatesKotlin(dir: File): List<File> {
        return runCatching {
            dir.walkTopDown()
                .filter { file ->
                    file.isFile && file.extension.lowercase(Locale.ROOT) in launchExts &&
                        file.relativeTo(dir).invariantSeparatorsPath.count { it == '/' } <= 3
                }
                .sortedWith(compareBy<File> { file ->
                    launchRank(file, dir)
                }.thenBy { it.relativeTo(dir).invariantSeparatorsPath.lowercase(Locale.ROOT) })
                .take(80)
                .toList()
        }.getOrDefault(emptyList())
    }

    private fun launchRank(file: File, root: File? = null): Int {
        val name = file.name.lowercase(Locale.ROOT)
        val preferredIndex = preferredLaunchNames.indexOf(name)
        if (preferredIndex >= 0) return preferredIndex

        val ext = file.extension.lowercase(Locale.ROOT)
        val base = file.nameWithoutExtension.lowercase(Locale.ROOT)
        val baseRank = when (ext) {
            "xp3" -> when {
                base == "boot" -> 20
                base == "main" || base == "game" || base == "scenario" || base == "script" -> 30
                base.startsWith("data") -> 40
                isAssetArchiveBase(base) -> 300
                else -> 80
            }
            "tjs" -> if (base == "main" || base == "boot" || base == "game") 60 else 90
            "ks" -> if (base == "first" || base == "scenario") 70 else 100
            else -> 500
        }
        val depthPenalty = root?.let {
            runCatching { file.relativeTo(it).invariantSeparatorsPath.count { ch -> ch == '/' } }.getOrDefault(0)
        } ?: 0
        return baseRank + depthPenalty * 20
    }

    private fun isAssetArchiveBase(base: String): Boolean {
        return base == "patch" ||
            base.startsWith("patch") ||
            base == "bg" ||
            base.startsWith("bg") ||
            base.contains("image") ||
            base.contains("voice") ||
            base.contains("sound") ||
            base.contains("audio") ||
            base.contains("music") ||
            base.contains("movie") ||
            base.contains("video") ||
            base.contains("effect")
    }

    private fun File.runCatchingReadText(): String? = runCatching { readText() }.getOrNull()
}
