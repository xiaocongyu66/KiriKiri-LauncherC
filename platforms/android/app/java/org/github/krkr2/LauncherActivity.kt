package org.github.krkr2

import android.content.Intent
import android.os.Bundle
import android.util.Log
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class LauncherActivity : AppCompatActivity() {
    private var resumeToken by mutableStateOf(0)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            LauncherTheme {
                LauncherScreen(
                    resumeToken = resumeToken,
                    onOpenSettings = { startActivity(Intent(this, LauncherSettingsActivity::class.java)) },
                    onLaunchGame = { game -> startGame(game) },
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        resumeToken++
    }

    private fun startGame(game: GameEntry) {
        LauncherPrefs.writeLauncherLog(this, "Launch game: ${game.gameDir} file=${game.launchFile.orEmpty()}")
        runCatching {
            val intent = Intent(this, MainActivity::class.java)
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
            intent.putExtra(MainActivity.EXTRA_GAME_DIR, game.gameDir)
            intent.putExtra(MainActivity.EXTRA_GAME_TITLE, game.title)
            game.launchFile?.let { intent.putExtra(MainActivity.EXTRA_LAUNCH_FILE, it) }
            startActivity(intent)
        }.onFailure { error ->
            LauncherPrefs.writeLauncherLog(this, "Failed to launch game: ${game.gameDir}", error)
            Log.e(TAG, "Failed to launch game: ${game.gameDir}", error)
        }
    }

    companion object {
        private const val TAG = "KR2Launcher"
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun LauncherScreen(
    resumeToken: Int,
    onOpenSettings: () -> Unit,
    onLaunchGame: (GameEntry) -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }
    var loading by remember { mutableStateOf(true) }
    var games by remember { mutableStateOf(emptyList<GameEntry>()) }
    var rootPath by remember { mutableStateOf(LauncherPrefs.getGameRoot(context)) }
    var lang by remember { mutableStateOf(LauncherPrefs.getLanguage(context)) }
    var selectedGame by remember { mutableStateOf<GameEntry?>(null) }
    var refreshToken by remember { mutableStateOf(0) }
    val text = if (lang == LauncherPrefs.LANG_ZH) LauncherStrings.zh else LauncherStrings.en

    fun reloadLibrary() {
        val savedRoot = LauncherPrefs.getGameRoot(context)
        rootPath = savedRoot
        lang = LauncherPrefs.getLanguage(context)
        loading = true
        scope.launch {
            val scanned = withContext(Dispatchers.IO) { GameScanner.scan(File(savedRoot)) }
            games = scanned
            selectedGame = selectedGame?.let { selected ->
                scanned.firstOrNull { it.gameDir == selected.gameDir }
            }
            refreshToken++
            loading = false
        }
    }

    LaunchedEffect(resumeToken) {
        reloadLibrary()
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbarHostState) },
        topBar = {
            TopAppBar(
                title = { Text(text.title) },
                navigationIcon = {
                    IconButton(onClick = { /* page indicator only */ }) {
                        Icon(Icons.Default.Home, contentDescription = text.title)
                    }
                },
                actions = {
                    IconButton(onClick = onOpenSettings) {
                        Icon(Icons.Default.Settings, contentDescription = text.settings)
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color(0xFF101014))
            )
        }
    ) { padding ->
        Surface(Modifier.fillMaxSize().padding(padding), color = Color(0xFF0C0C10)) {
            Row(Modifier.fillMaxSize().padding(16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                LibrarySideBar(text, games, rootPath, onOpenSettings)
                GameGrid(
                    modifier = Modifier.weight(1f),
                    loading = loading,
                    games = games,
                    text = text,
                    refreshToken = refreshToken,
                    onSelect = { selectedGame = it },
                )
                selectedGame?.let { game ->
                    GameDetailPanel(
                        game = game,
                        text = text,
                        refreshToken = refreshToken,
                        onSaved = { refreshToken++ },
                        onClose = { selectedGame = null },
                        onLaunch = { onLaunchGame(it) },
                    )
                }
            }
        }
    }
}

@Composable
private fun LibrarySideBar(
    text: LauncherStrings.Texts,
    games: List<GameEntry>,
    rootPath: String,
    onSettings: () -> Unit,
) {
    val context = LocalContext.current
    val totalLaunches = games.sumOf { LauncherPrefs.getStats(context, it.gameDir).launchCount }
    Column(
        modifier = Modifier.width(116.dp).fillMaxHeight(),
        verticalArrangement = Arrangement.spacedBy(10.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        IconButton(onClick = onSettings) { Icon(Icons.Default.Settings, null, tint = Color.White) }
        Text(text.settings, color = Color(0xFFCCCCCC), style = MaterialTheme.typography.bodySmall)
        Spacer(Modifier.height(8.dp))
        Text(text.stats, color = Color.White, style = MaterialTheme.typography.bodySmall, fontWeight = FontWeight.Bold)
        Text("${games.size}", color = Color(0xFFCCCCCC), style = MaterialTheme.typography.bodySmall)
        Text("${text.launches}: $totalLaunches", color = Color(0xFFCCCCCC), style = MaterialTheme.typography.bodySmall)
        Spacer(Modifier.weight(1f))
        Text(rootPath, color = Color(0xFF777777), style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun GameGrid(
    modifier: Modifier,
    loading: Boolean,
    games: List<GameEntry>,
    text: LauncherStrings.Texts,
    refreshToken: Int,
    onSelect: (GameEntry) -> Unit,
) {
    Box(modifier.fillMaxSize()) {
        when {
            loading -> CircularProgressIndicator(Modifier.align(Alignment.Center))
            games.isEmpty() -> Column(Modifier.align(Alignment.Center), horizontalAlignment = Alignment.CenterHorizontally) {
                Text(text.noGamesFound, style = MaterialTheme.typography.headlineMedium, color = Color.White)
                Spacer(Modifier.height(8.dp))
                Text(text.emptyHint, color = Color(0xFFBBBBBB))
            }
            else -> LazyVerticalGrid(
                columns = GridCells.Adaptive(180.dp),
                contentPadding = PaddingValues(bottom = 16.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                items(games, key = { it.gameDir }) { game ->
                    GameCard(game = game, text = text, refreshToken = refreshToken, onSelect = onSelect)
                }
            }
        }
    }
}

@Composable
private fun GameCard(game: GameEntry, text: LauncherStrings.Texts, refreshToken: Int, onSelect: (GameEntry) -> Unit) {
    val context = LocalContext.current
    val stats = LauncherPrefs.getStats(context, game.gameDir)
    val image = LauncherPrefs.getCustomImagePath(context, game.gameDir)?.takeIf { it.isNotBlank() } ?: game.coverPath ?: game.backgroundPath
    // Force recomposition after alias/image changes saved from the detail panel.
    val ignoredRefreshToken = refreshToken
    ElevatedCard(
        modifier = Modifier.fillMaxWidth().aspectRatio(0.78f).clickable { onSelect(game) },
        shape = RoundedCornerShape(24.dp),
        colors = CardDefaults.elevatedCardColors(containerColor = Color(0xFF17171D))
    ) {
        Box(Modifier.fillMaxSize()) {
            AsyncImage(model = image, contentDescription = null, contentScale = ContentScale.Crop, modifier = Modifier.fillMaxSize())
            Box(Modifier.fillMaxSize().background(Brush.verticalGradient(0f to Color.Transparent, 0.65f to Color(0xAA000000), 1f to Color(0xEE000000))))
            Column(modifier = Modifier.fillMaxSize().padding(14.dp), verticalArrangement = Arrangement.Bottom) {
                Text(LauncherPrefs.displayName(context, game), color = Color.White, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
                Text(File(game.gameDir).name, color = Color(0xFFCCCCCC), style = MaterialTheme.typography.bodySmall)
                Text("${text.launches}: ${stats.launchCount}  ${text.playTime}: ${stats.formatPlayTime()}", color = Color(0xFFCCCCCC), style = MaterialTheme.typography.bodySmall)
                if (ignoredRefreshToken < 0) Text("")
            }
        }
    }
}

@Composable
private fun GameDetailPanel(
    game: GameEntry,
    text: LauncherStrings.Texts,
    refreshToken: Int,
    onSaved: () -> Unit,
    onClose: () -> Unit,
    onLaunch: (GameEntry) -> Unit,
) {
    val context = LocalContext.current
    val stats = LauncherPrefs.getStats(context, game.gameDir)
    var alias by remember(game.gameDir, refreshToken) { mutableStateOf(LauncherPrefs.getAlias(context, game.gameDir).orEmpty()) }
    var imagePath by remember(game.gameDir, refreshToken) { mutableStateOf(LauncherPrefs.getCustomImagePath(context, game.gameDir).orEmpty()) }
    val image = imagePath.takeIf { it.isNotBlank() } ?: game.coverPath ?: game.backgroundPath
    val displayTitle = alias.trim().ifBlank { game.title }

    fun saveDetail() {
        LauncherPrefs.setAlias(context, game.gameDir, alias)
        LauncherPrefs.setCustomImagePath(context, game.gameDir, imagePath)
        onSaved()
    }

    ElevatedCard(
        modifier = Modifier.fillMaxHeight().width(420.dp),
        shape = RoundedCornerShape(28.dp),
        colors = CardDefaults.elevatedCardColors(containerColor = Color(0xFF17171D))
    ) {
        Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            AsyncImage(model = image, contentDescription = null, contentScale = ContentScale.Crop, modifier = Modifier.fillMaxWidth().height(180.dp))
            Text(displayTitle, color = Color.White, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
            Text(game.gameDir, color = Color(0xFFBBBBBB), style = MaterialTheme.typography.bodySmall)
            Text("UUID: ${stats.uuid}", color = Color(0xFF999999), style = MaterialTheme.typography.bodySmall)
            Text("${text.launches}: ${stats.launchCount}", color = Color.White)
            Text("${text.playTime}: ${stats.formatPlayTime()}", color = Color.White)
            OutlinedTextField(value = alias, onValueChange = { alias = it }, modifier = Modifier.fillMaxWidth(), label = { Text(text.alias) })
            OutlinedTextField(value = imagePath, onValueChange = { imagePath = it }, modifier = Modifier.fillMaxWidth(), label = { Text(text.customImagePath) })
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                FilledTonalButton(onClick = { saveDetail() }) { Text(text.save) }
                FilledTonalButton(onClick = { saveDetail(); onLaunch(game) }) { Icon(Icons.Default.PlayArrow, null); Text(text.start) }
                FilledTonalButton(onClick = onClose) { Text(text.close) }
            }
        }
    }
}
