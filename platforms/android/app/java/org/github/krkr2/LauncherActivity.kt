package org.github.krkr2

import android.os.Bundle
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
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ElevatedAssistChip
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
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
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            LauncherTheme {
                LauncherScreen(
                    onOpenSettings = { startActivity(android.content.Intent(this, LauncherSettingsActivity::class.java)) },
                    onLaunchGame = { game -> startGame(game.gameDir, game.title) },
                    onLaunchOriginal = { startOriginal() },
                    onRequestPermission = { requestStoragePermission() },
                )
            }
        }
    }

    private fun startGame(gameDir: String, title: String) {
        val intent = android.content.Intent(this, MainActivity::class.java)
        intent.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK or android.content.Intent.FLAG_ACTIVITY_CLEAR_TASK)
        intent.putExtra(MainActivity.EXTRA_GAME_DIR, gameDir)
        intent.putExtra(MainActivity.EXTRA_GAME_TITLE, title)
        startActivity(intent)
    }

    private fun startOriginal() {
        val intent = android.content.Intent(this, MainActivity::class.java)
        intent.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK or android.content.Intent.FLAG_ACTIVITY_CLEAR_TASK)
        startActivity(intent)
    }

    private fun requestStoragePermission() {
        val intent = android.content.Intent(
            android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
            android.net.Uri.fromParts("package", packageName, null)
        )
        startActivity(intent)
    }
}

@Composable
private fun LauncherScreen(
    onOpenSettings: () -> Unit,
    onLaunchGame: (GameEntry) -> Unit,
    onLaunchOriginal: () -> Unit,
    onRequestPermission: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }
    var loading by remember { mutableStateOf(true) }
    var games by remember { mutableStateOf(emptyList<GameEntry>()) }
    var rootPath by remember { mutableStateOf(LauncherPrefs.getGameRoot(context)) }
    var editPath by remember { mutableStateOf(rootPath) }
    var forceLandscape by remember { mutableStateOf(LauncherPrefs.getForceLandscape(context)) }
    var lang by remember { mutableStateOf(LauncherPrefs.getLanguage(context)) }
    var selectedGame by remember { mutableStateOf<GameEntry?>(null) }
    val text = if (lang == LauncherPrefs.LANG_ZH) LauncherStrings.zh else LauncherStrings.en

    fun rescan(path: String) {
        val normalized = path.trim().ifBlank { LauncherPrefs.DEFAULT_GAME_ROOT }
        rootPath = normalized
        editPath = normalized
        LauncherPrefs.setGameRoot(context, normalized)
        loading = true
        scope.launch {
            games = withContext(Dispatchers.IO) { GameScanner.scan(File(normalized)) }
            loading = false
        }
    }

    LaunchedEffect(rootPath) {
        loading = true
        games = withContext(Dispatchers.IO) { GameScanner.scan(File(rootPath)) }
        loading = false
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbarHostState) },
        topBar = {
            TopAppBar(
                title = { Text(text.title) },
                actions = {
                    IconButton(onClick = { scope.launch { snackbarHostState.showSnackbar(text.scan) }; rescan(rootPath) }) { Icon(Icons.Default.Refresh, null) }
                    IconButton(onClick = onOpenSettings) { Icon(Icons.Default.Settings, null) }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color(0xFF101014))
            )
        }
    ) { padding ->
        Surface(Modifier.fillMaxSize().padding(padding), color = Color(0xFF0C0C10)) {
            Row(Modifier.fillMaxSize().padding(16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                SideBar(text, games, onOpenSettings, onLaunchOriginal) { rescan(editPath) }
                Column(Modifier.weight(1f)) {
                    TopControls(
                        text = text,
                        editPath = editPath,
                        onEditPath = { editPath = it },
                        onRequestPermission = onRequestPermission,
                        onOpenSettings = onOpenSettings,
                        onScan = { rescan(editPath) },
                        onReloadSaved = { editPath = LauncherPrefs.getGameRoot(context); rescan(editPath) },
                        onLaunchOriginal = onLaunchOriginal,
                        onExport = {
                            val file = LauncherPrefs.exportBackup(context)
                            scope.launch { snackbarHostState.showSnackbar("${text.exported}: ${file.absolutePath}") }
                        },
                        forceLandscape = forceLandscape,
                        onForceLandscape = { forceLandscape = it; LauncherPrefs.setForceLandscape(context, it) },
                        onLangEn = { LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_EN); lang = LauncherPrefs.LANG_EN },
                        onLangZh = { LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_ZH); lang = LauncherPrefs.LANG_ZH },
                    )
                    Spacer(Modifier.height(16.dp))
                    GameGrid(
                        loading = loading,
                        games = games,
                        text = text,
                        onSelect = { selectedGame = it },
                    )
                }
                selectedGame?.let { game ->
                    GameDetailPanel(
                        game = game,
                        text = text,
                        onClose = { selectedGame = null },
                        onLaunch = { onLaunchGame(it) },
                    )
                }
            }
        }
    }
}

@Composable
private fun SideBar(text: LauncherStrings.Texts, games: List<GameEntry>, onSettings: () -> Unit, onOriginal: () -> Unit, onScan: () -> Unit) {
    val context = LocalContext.current
    Column(modifier = Modifier.width(88.dp), verticalArrangement = Arrangement.spacedBy(10.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        IconButton(onClick = onSettings) { Icon(Icons.Default.Settings, null, tint = Color.White) }
        IconButton(onClick = onOriginal) { Icon(Icons.Default.PlayArrow, null, tint = Color.White) }
        IconButton(onClick = onScan) { Icon(Icons.Default.Refresh, null, tint = Color.White) }
        Text(text.stats, color = Color.White, style = MaterialTheme.typography.bodySmall)
        Text("${games.sumOf { LauncherPrefs.getStats(context, it.gameDir).launchCount }}", color = Color(0xFFCCCCCC), style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun TopControls(
    text: LauncherStrings.Texts,
    editPath: String,
    onEditPath: (String) -> Unit,
    onRequestPermission: () -> Unit,
    onOpenSettings: () -> Unit,
    onScan: () -> Unit,
    onReloadSaved: () -> Unit,
    onLaunchOriginal: () -> Unit,
    onExport: () -> Unit,
    forceLandscape: Boolean,
    onForceLandscape: (Boolean) -> Unit,
    onLangEn: () -> Unit,
    onLangZh: () -> Unit,
) {
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
        ElevatedAssistChip(onClick = onRequestPermission, label = { Text(text.grantStorage) }, leadingIcon = { Icon(Icons.Default.FolderOpen, null) })
        ElevatedAssistChip(onClick = onOpenSettings, label = { Text(text.settings) }, leadingIcon = { Icon(Icons.Default.Settings, null) })
        ElevatedAssistChip(onClick = onScan, label = { Text(text.scan) }, leadingIcon = { Icon(Icons.Default.Refresh, null) })
    }
    Spacer(Modifier.height(12.dp))
    OutlinedTextField(value = editPath, onValueChange = onEditPath, modifier = Modifier.fillMaxWidth(), label = { Text(text.gameRootPath) }, singleLine = true)
    Spacer(Modifier.height(8.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        FilledTonalButton(onClick = onScan) { Text(text.saveAndScan) }
        FilledTonalButton(onClick = onReloadSaved) { Text(text.reloadSaved) }
        FilledTonalButton(onClick = onLaunchOriginal) { Text(text.launchOriginal) }
        FilledTonalButton(onClick = onExport) { Text(text.exportBackup) }
    }
    Spacer(Modifier.height(8.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
        FilledTonalButton(onClick = onLangEn) { Text(text.english) }
        FilledTonalButton(onClick = onLangZh) { Text(text.chinese) }
        Text(text.forceLandscape, color = Color.White)
        Switch(checked = forceLandscape, onCheckedChange = onForceLandscape)
    }
}

@Composable
private fun GameGrid(loading: Boolean, games: List<GameEntry>, text: LauncherStrings.Texts, onSelect: (GameEntry) -> Unit) {
    if (loading) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
    } else if (games.isEmpty()) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(text.noGamesFound, style = MaterialTheme.typography.headlineMedium, color = Color.White)
                Spacer(Modifier.height(8.dp))
                Text(text.emptyHint, color = Color(0xFFBBBBBB))
            }
        }
    } else {
        LazyVerticalGrid(columns = GridCells.Adaptive(180.dp), contentPadding = PaddingValues(bottom = 16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            items(games, key = { it.gameDir }) { game -> GameCard(game = game, text = text, onSelect = onSelect) }
        }
    }
}

@Composable
private fun GameCard(game: GameEntry, text: LauncherStrings.Texts, onSelect: (GameEntry) -> Unit) {
    val context = LocalContext.current
    val stats = LauncherPrefs.getStats(context, game.gameDir)
    val image = LauncherPrefs.getCustomImagePath(context, game.gameDir)?.takeIf { it.isNotBlank() } ?: game.coverPath ?: game.backgroundPath
    ElevatedCard(modifier = Modifier.fillMaxWidth().aspectRatio(0.78f).clickable { onSelect(game) }, shape = RoundedCornerShape(24.dp), colors = CardDefaults.elevatedCardColors(containerColor = Color(0xFF17171D))) {
        Box(Modifier.fillMaxSize()) {
            AsyncImage(model = image, contentDescription = null, contentScale = ContentScale.Crop, modifier = Modifier.fillMaxSize())
            Box(Modifier.fillMaxSize().background(Brush.verticalGradient(0f to Color.Transparent, 0.65f to Color(0xAA000000), 1f to Color(0xEE000000))))
            Column(modifier = Modifier.fillMaxSize().padding(14.dp), verticalArrangement = Arrangement.Bottom) {
                Text(LauncherPrefs.displayName(context, game), color = Color.White, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
                Text(File(game.gameDir).name, color = Color(0xFFCCCCCC), style = MaterialTheme.typography.bodySmall)
                Text("${text.launches}: ${stats.launchCount}  ${text.playTime}: ${stats.formatPlayTime()}", color = Color(0xFFCCCCCC), style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
private fun GameDetailPanel(game: GameEntry, text: LauncherStrings.Texts, onClose: () -> Unit, onLaunch: (GameEntry) -> Unit) {
    val context = LocalContext.current
    val stats = LauncherPrefs.getStats(context, game.gameDir)
    var alias by remember(game.gameDir) { mutableStateOf(LauncherPrefs.getAlias(context, game.gameDir).orEmpty()) }
    var imagePath by remember(game.gameDir) { mutableStateOf(LauncherPrefs.getCustomImagePath(context, game.gameDir).orEmpty()) }
    val image = imagePath.takeIf { it.isNotBlank() } ?: game.coverPath ?: game.backgroundPath

    ElevatedCard(modifier = Modifier.fillMaxHeight().width(420.dp), shape = RoundedCornerShape(28.dp), colors = CardDefaults.elevatedCardColors(containerColor = Color(0xFF17171D))) {
        Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            AsyncImage(model = image, contentDescription = null, contentScale = ContentScale.Crop, modifier = Modifier.fillMaxWidth().height(180.dp))
            Text(LauncherPrefs.displayName(context, game), color = Color.White, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
            Text(game.gameDir, color = Color(0xFFBBBBBB), style = MaterialTheme.typography.bodySmall)
            Text("UUID: ${stats.uuid}", color = Color(0xFF999999), style = MaterialTheme.typography.bodySmall)
            Text("${text.launches}: ${stats.launchCount}", color = Color.White)
            Text("${text.playTime}: ${stats.formatPlayTime()}", color = Color.White)
            OutlinedTextField(value = alias, onValueChange = { alias = it }, modifier = Modifier.fillMaxWidth(), label = { Text(text.alias) })
            OutlinedTextField(value = imagePath, onValueChange = { imagePath = it }, modifier = Modifier.fillMaxWidth(), label = { Text(text.customImagePath) })
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                FilledTonalButton(onClick = { LauncherPrefs.setAlias(context, game.gameDir, alias); LauncherPrefs.setCustomImagePath(context, game.gameDir, imagePath) }) { Text(text.save) }
                FilledTonalButton(onClick = { LauncherPrefs.setAlias(context, game.gameDir, alias); LauncherPrefs.setCustomImagePath(context, game.gameDir, imagePath); onLaunch(game) }) { Icon(Icons.Default.PlayArrow, null); Text(text.start) }
                FilledTonalButton(onClick = onClose) { Text("OK") }
            }
        }
    }
}