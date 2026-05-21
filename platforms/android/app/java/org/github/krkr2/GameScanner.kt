package org.github.krkr2

import java.io.File
import java.util.Locale

data class GameEntry(
    val title: String,
    val gameDir: String,
    val coverPath: String? = null,
    val backgroundPath: String? = null,
    val launchFile: String? = null,
    val lastModified: Long = 0L,
)

object GameScanner {
    private val gameMarkers = setOf(
        "startup.tjs", "start.tjs", "data.xp3", "patch.xp3", "scenario.ks", "first.ks", "config.tjs"
    )
    private val imageExts = setOf("jpg", "jpeg", "png", "webp")
    private val coverNames = listOf(
        "cover", "icon", "title", "thumb", "thumbnail", "package", "bg", "background", "main"
    )

    fun scan(root: File, maxDepth: Int = 6): List<GameEntry> {
        if (!root.exists() || !root.isDirectory) return emptyList()
        val result = linkedMapOf<String, GameEntry>()
        scanDir(root, 0, maxDepth, result)
        return result.values.sortedWith(
            compareByDescending<GameEntry> { it.lastModified }.thenBy { it.title.lowercase(Locale.ROOT) }
        )
    }

    private fun scanDir(dir: File, depth: Int, maxDepth: Int, result: MutableMap<String, GameEntry>) {
        if (depth > maxDepth || dir.name.startsWith(".")) return
        val children = runCatching { dir.listFiles()?.toList().orEmpty() }.getOrDefault(emptyList())
        if (children.isEmpty()) return

        if (isGameDir(dir, children)) {
            val entry = buildGameEntry(dir, children)
            result[entry.gameDir] = entry
            return
        }

        children.asSequence()
            .filter { it.isDirectory }
            .filterNot { it.name.equals("Android", true) || it.name.startsWith(".") }
            .forEach { scanDir(it, depth + 1, maxDepth, result) }
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
        val order = listOf("startup.tjs", "start.tjs", "data.xp3")
        return order.firstNotNullOfOrNull { name -> children.firstOrNull { it.name.equals(name, true) } }
            ?: children.firstOrNull { it.isFile && it.extension.equals("xp3", true) }
            ?: children.firstOrNull { it.isFile && it.extension.equals("ks", true) }
    }

    private fun File.runCatchingReadText(): String? = runCatching { readText() }.getOrNull()
}
