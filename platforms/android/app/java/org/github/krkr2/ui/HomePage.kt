package org.github.krkr2.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import org.github.krkr2.LauncherStrings
import org.github.krkr2.data.GameInfo
import org.github.krkr2.data.GameManager
import org.github.krkr2.data.TimeFormat
import org.github.krkr2.ui.components.CoverCard
import org.github.krkr2.ui.components.EmptyState

/**
 * Launcher home page — game library grid.
 *
 * Layout, ported from KrKr2-Next's `HomePage`:
 *   - Big bold title row with a Settings icon button on the right.
 *   - Adaptive grid of CoverCards (3 : 4 aspect, target tile width = 160dp).
 *   - FAB "Refresh" for re-scan.
 *   - Long-press a card to open the contextual menu.
 */
@Composable
fun HomePage(
    text: LauncherStrings.Texts,
    onOpenSettings: () -> Unit,
    onOpenDetail: (GameInfo) -> Unit,
    onLaunch: (GameInfo) -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val gameManager = remember { GameManager(context) }

    var loading by remember { mutableStateOf(true) }
    var games by remember { mutableStateOf(emptyList<GameInfo>()) }
    var reloadToken by remember { mutableStateOf(0) }

    LaunchedEffect(reloadToken) {
        loading = true
        games = gameManager.load()
        loading = false
    }

    Scaffold(
        topBar = { LibraryHeader(text = text, onOpenSettings = onOpenSettings) },
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = { reloadToken++ },
                icon = { Icon(Icons.Default.Refresh, contentDescription = null) },
                text = { Text(text.refresh) },
            )
        },
    ) { padding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
        ) {
            when {
                loading -> CircularProgressIndicator(Modifier.align(Alignment.Center))
                games.isEmpty() -> EmptyState(
                    title = text.noGamesYet,
                    hint = text.noGamesHint,
                )
                else -> GameGrid(
                    games = games,
                    text = text,
                    onOpenDetail = onOpenDetail,
                    onLaunch = onLaunch,
                    onClearCover = { game ->
                        scope.launch {
                            gameManager.setCover(game, null)
                            reloadToken++
                        }
                    },
                )
            }
        }
    }
}

@Composable
private fun LibraryHeader(
    text: LauncherStrings.Texts,
    onOpenSettings: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .windowInsetsPadding(WindowInsets.statusBars)
            .padding(start = 20.dp, end = 8.dp, top = 16.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = text.title,
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.onBackground,
            modifier = Modifier.weight(1f),
        )
        IconButton(onClick = onOpenSettings) {
            Icon(Icons.Default.Settings, contentDescription = text.settings)
        }
    }
}

@Composable
private fun GameGrid(
    games: List<GameInfo>,
    text: LauncherStrings.Texts,
    onOpenDetail: (GameInfo) -> Unit,
    onLaunch: (GameInfo) -> Unit,
    onClearCover: (GameInfo) -> Unit,
) {
    LazyVerticalGrid(
        columns = GridCells.Adaptive(minSize = 160.dp),
        contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 96.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        modifier = Modifier.fillMaxSize(),
    ) {
        items(games, key = { it.path }) { game ->
            var menuOpen by remember { mutableStateOf(false) }
            Box {
                CoverCard(
                    game = game,
                    subtitle = buildSubtitle(game, text),
                    onClick = { onOpenDetail(game) },
                    onLongPress = { menuOpen = true },
                    modifier = Modifier.aspectRatio(0.75f),
                )
                DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                    DropdownMenuItem(
                        text = { Text(text.launchGame) },
                        onClick = { menuOpen = false; onLaunch(game) },
                    )
                    DropdownMenuItem(
                        text = { Text(text.openInLauncher) },
                        onClick = { menuOpen = false; onOpenDetail(game) },
                    )
                    DropdownMenuItem(
                        text = { Text(text.clearCover) },
                        onClick = { menuOpen = false; onClearCover(game) },
                    )
                }
            }
        }
    }
}

private fun buildSubtitle(game: GameInfo, text: LauncherStrings.Texts): String? {
    val pieces = buildList {
        if (game.lastPlayedMillis > 0L) {
            add(TimeFormat.relativeDate(game.lastPlayedMillis, text))
        }
        if (game.playDurationSeconds >= 60L) {
            add(TimeFormat.playDuration(game.playDurationSeconds))
        }
    }
    return pieces.takeIf { it.isNotEmpty() }?.joinToString(" · ")
}