package org.github.krkr2.data

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.github.krkr2.GameEntry
import org.github.krkr2.GameScanner
import org.github.krkr2.LauncherPrefs
import java.io.File

/**
 * GameManager — facade over [LauncherPrefs] + [GameScanner], modelled after
 * KrKr2-Next's `services/game_manager.dart`.
 *
 * Responsibilities:
 *   - Scan the configured game root and produce [GameInfo] objects.
 *   - Merge filesystem state with per-game user overrides (alias / cover /
 *     developer) and play statistics.
 *   - Expose mutation helpers (rename / setCover / setDeveloper) that
 *     delegate to [LauncherPrefs] for persistence.
 *
 * Stateless on purpose: every read goes back to SharedPreferences so the
 * Compose layer can simply call [load] inside a LaunchedEffect to get a
 * consistent snapshot after returning from MainActivity.
 */
class GameManager(private val context: Context) {

    /** Scan disk and return the full library, sorted lastPlayed-desc then title. */
    suspend fun load(): List<GameInfo> = withContext(Dispatchers.IO) {
        val rootPath = LauncherPrefs.getGameRoot(context)
        val root = File(rootPath)
        val entries = GameScanner.scan(root)
        entries
            .map { entry -> entry.toGameInfo(context) }
            .sortedWith(
                compareByDescending<GameInfo> { it.lastPlayedMillis }
                    .thenByDescending { it.launchCount }
                    .thenBy { it.displayTitle.lowercase() }
            )
    }

    suspend fun refresh(game: GameInfo): GameInfo? = withContext(Dispatchers.IO) {
        val all = load()
        all.firstOrNull { it.path == game.path }
    }

    fun rename(game: GameInfo, newTitle: String) {
        LauncherPrefs.setAlias(context, game.path, newTitle)
    }

    fun setCover(game: GameInfo, coverPath: String?) {
        LauncherPrefs.setCustomImagePath(context, game.path, coverPath.orEmpty())
    }

    fun setDeveloper(game: GameInfo, developer: String?) {
        LauncherPrefs.setDeveloper(context, game.path, developer.orEmpty())
    }

    /** Convenience accessor used by HomePage to count games / launches. */
    fun totalLaunches(games: List<GameInfo>): Int = games.sumOf { it.launchCount }
}

private fun GameEntry.toGameInfo(context: Context): GameInfo {
    val overrides = GameOverrides(
        alias = LauncherPrefs.getAlias(context, gameDir),
        developer = LauncherPrefs.getDeveloper(context, gameDir),
        customCoverPath = LauncherPrefs.getCustomImagePath(context, gameDir),
    )
    val stats = LauncherPrefs.getStats(context, gameDir).let { s ->
        PlayStats(
            launchCount = s.launchCount,
            playDurationSeconds = s.playTimeMillis / 1000L,
            lastPlayedMillis = s.lastLaunchMillis,
        )
    }
    return GameInfo.fromEntry(this, overrides, stats)
}
