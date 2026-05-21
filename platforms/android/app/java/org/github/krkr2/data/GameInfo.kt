package org.github.krkr2.data

import org.github.krkr2.GameEntry

/**
 * Lightweight presentation model for a game, mirroring the field set of
 * KrKr2-Next's `GameInfo`. We keep this separate from [GameEntry] (the
 * filesystem-scanner DTO) so the UI layer is decoupled from disk shape.
 *
 *  - [path]      absolute game directory or .xp3 file (stable identity)
 *  - [title]     display title (falls back to entry.title → dir name)
 *  - [developer] optional vendor/developer string (set via metadata scrape)
 *  - [coverPath] user-set or auto-detected cover image file path
 *  - [bgPath]    secondary image for detail page hero (optional)
 *  - [launchFile] preferred entry file passed to native (data.xp3/startup.tjs)
 *  - [lastPlayedMillis] epoch millis of last launch (0 = never)
 *  - [launchCount]     number of completed launches recorded
 *  - [playDurationSeconds] cumulative play time
 */
data class GameInfo(
    val path: String,
    val title: String,
    val developer: String? = null,
    val coverPath: String? = null,
    val bgPath: String? = null,
    val launchFile: String? = null,
    val lastPlayedMillis: Long = 0L,
    val launchCount: Int = 0,
    val playDurationSeconds: Long = 0L,
) {
    val displayTitle: String get() = title.ifBlank { path.substringAfterLast('/').ifBlank { path } }

    val isXp3: Boolean get() = path.lowercase().endsWith(".xp3")

    companion object {
        fun fromEntry(entry: GameEntry, overrides: GameOverrides? = null, stats: PlayStats? = null): GameInfo {
            val effectiveTitle = overrides?.alias?.takeIf { it.isNotBlank() } ?: entry.title
            val effectiveCover = overrides?.customCoverPath?.takeIf { it.isNotBlank() } ?: entry.coverPath
            return GameInfo(
                path = entry.gameDir,
                title = effectiveTitle,
                developer = overrides?.developer,
                coverPath = effectiveCover,
                bgPath = entry.backgroundPath,
                launchFile = entry.launchFile,
                lastPlayedMillis = stats?.lastPlayedMillis ?: 0L,
                launchCount = stats?.launchCount ?: 0,
                playDurationSeconds = stats?.playDurationSeconds ?: 0L,
            )
        }
    }
}

/** Per-game user overrides persisted in SharedPreferences. */
data class GameOverrides(
    val alias: String? = null,
    val developer: String? = null,
    val customCoverPath: String? = null,
)

/** Per-game play statistics persisted in SharedPreferences. */
data class PlayStats(
    val launchCount: Int = 0,
    val playDurationSeconds: Long = 0L,
    val lastPlayedMillis: Long = 0L,
)
