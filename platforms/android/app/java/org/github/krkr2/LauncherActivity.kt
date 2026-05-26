package org.github.krkr2

import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ElevatedAssistChip
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberModalBottomSheetState
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
    // The launcher itself does NOT force landscape. We let the system
    // decide the activity orientation (the manifest declares
    // screenOrientation="unspecified") so:
    //   - phones in portrait get a phone-shaped UI
    //   - tablets always get the wide UI
    //   - phones turned sideways auto-switch to the wide UI too
    // The "force landscape" preference only applies to MainActivity (the
    // game), which is what the user actually wants to play in landscape.

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            LauncherTheme {
                LauncherScreen(
                    onOpenSettings = { startActivity(android.content.Intent(this, LauncherSettingsActivity::class.java)) },
                    onOpenDiagnostics = { startActivity(android.content.Intent(this, DiagnosticsActivity::class.java)) },
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
        // Per-game launch file override. Only attach the extra when the
        // user explicitly picked one and the file still exists. Otherwise
        // KR2Activity.resolveLaunchGamePath() does its own auto detect on
        // the gameDir (startup.tjs / start.tjs / data.xp3 / first .xp3 /
        // first .ks). Validating here avoids handing native code a stale
        // path that points at a deleted file.
        LauncherPrefs.getCustomLaunchFile(this, gameDir)
            ?.takeIf { it.isNotBlank() }
            ?.let { path ->
                val f = java.io.File(path)
                if (f.isFile && f.canRead()) {
                    intent.putExtra(MainActivity.EXTRA_LAUNCH_FILE, f.absolutePath)
                }
            }
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

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun LauncherScreen(
    onOpenSettings: () -> Unit,
    onOpenDiagnostics: () -> Unit,
    onLaunchGame: (GameEntry) -> Unit,
    onLaunchOriginal: () -> Unit,
    onRequestPermission: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }
    // Seed with cache so the list paints instantly on cold start.
    // The full scan still runs in the background and replaces the
    // list when it finishes; the verifier pass also prunes stale
    // entries (deleted dirs, empty dirs) before the scan completes.
    val cachedSeed = remember { GameCache.load(context) }
    var loading by remember { mutableStateOf(cachedSeed.isEmpty()) }
    var games by remember { mutableStateOf(cachedSeed) }
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
            val fresh = withContext(Dispatchers.IO) { GameScanner.scan(File(normalized)) }
            games = fresh
            loading = false
            withContext(Dispatchers.IO) {
                GameCache.setCachedRoot(context, normalized)
                GameCache.save(context, fresh)
            }
        }
    }

    LaunchedEffect(rootPath) {
        // Two-phase load:
        //   1. Background verify: walk the existing cache and drop any
        //      entry whose backing directory is gone or empty. Cheap (one
        //      stat per entry) and lets us prune deleted games before
        //      the slower full scan finishes.
        //   2. Full scan: re-walk the root and replace the list. Saves
        //      back to cache on success.
        // If the cache is empty (first run, or previous root differed),
        // we skip phase 1 and show the spinner until phase 2 completes.
        val cachedRoot = withContext(Dispatchers.IO) { GameCache.getCachedRoot(context) }
        val haveValidCache = games.isNotEmpty() && cachedRoot == rootPath
        if (haveValidCache) {
            // Run verification on a worker; UI keeps showing the cached
            // list until verification completes, then we show only the
            // surviving entries before the full rescan replaces them.
            val verified = withContext(Dispatchers.IO) {
                games.filter { GameCache.checkEntry(it) == GameCache.EntryStatus.VALID }
            }
            if (verified.size != games.size) {
                games = verified
                withContext(Dispatchers.IO) { GameCache.save(context, verified) }
            }
        } else {
            loading = true
        }
        // Always re-walk the root afterwards so freshly-added games get
        // picked up. This is the slow path; cache + verifier are what
        // make the cold-start feel snappy.
        val fresh = withContext(Dispatchers.IO) { GameScanner.scan(File(rootPath)) }
        games = fresh
        loading = false
        withContext(Dispatchers.IO) {
            GameCache.setCachedRoot(context, rootPath)
            GameCache.save(context, fresh)
        }
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbarHostState) },
        topBar = {
            TopAppBar(
                title = { Text(text.title) },
                actions = {
                    IconButton(onClick = { scope.launch { snackbarHostState.showSnackbar(text.scan) }; rescan(rootPath) }) { Icon(Icons.Default.Refresh, null) }
                    IconButton(onClick = onOpenDiagnostics) { Icon(Icons.Default.Info, null) }
                    IconButton(onClick = onOpenSettings) { Icon(Icons.Default.Settings, null) }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color(0xFF101014))
            )
        }
    ) { padding ->
        Surface(Modifier.fillMaxSize().padding(padding), color = Color(0xFF0C0C10)) {
            BoxWithConstraints(Modifier.fillMaxSize()) {
                // Pick phone vs tablet UI by the actual layout space, not
                // the activity rotation. Anything with at least 600dp
                // smallest screen width OR currently wider than tall gets
                // the wide layout.
                val cfg = context.resources.configuration
                val wide = cfg.smallestScreenWidthDp >= 600 || maxWidth > maxHeight
                if (wide) {
                    LandscapeLayout(
                        text = text,
                        games = games,
                        loading = loading,
                        editPath = editPath,
                        onEditPath = { editPath = it },
                        rescan = ::rescan,
                        forceLandscape = forceLandscape,
                        onForceLandscape = {
                            forceLandscape = it
                            LauncherPrefs.setForceLandscape(context, it)
                        },
                        lang = lang,
                        onLang = {
                            LauncherPrefs.setLanguage(context, it)
                            lang = it
                        },
                        onSelect = { selectedGame = it },
                        selectedGame = selectedGame,
                        onCloseDetail = { selectedGame = null },
                        onLaunchGame = onLaunchGame,
                        onOpenSettings = onOpenSettings,
                        onOpenDiagnostics = onOpenDiagnostics,
                        onLaunchOriginal = onLaunchOriginal,
                        onRequestPermission = onRequestPermission,
                        onExportSnack = { msg ->
                            scope.launch { snackbarHostState.showSnackbar(msg) }
                        },
                    )
                } else {
                    PortraitLayout(
                        text = text,
                        games = games,
                        loading = loading,
                        editPath = editPath,
                        onEditPath = { editPath = it },
                        rescan = ::rescan,
                        forceLandscape = forceLandscape,
                        onForceLandscape = {
                            forceLandscape = it
                            LauncherPrefs.setForceLandscape(context, it)
                        },
                        lang = lang,
                        onLang = {
                            LauncherPrefs.setLanguage(context, it)
                            lang = it
                        },
                        onSelect = { selectedGame = it },
                        selectedGame = selectedGame,
                        onCloseDetail = { selectedGame = null },
                        onLaunchGame = onLaunchGame,
                        onOpenSettings = onOpenSettings,
                        onOpenDiagnostics = onOpenDiagnostics,
                        onLaunchOriginal = onLaunchOriginal,
                        onRequestPermission = onRequestPermission,
                        onExportSnack = { msg ->
                            scope.launch { snackbarHostState.showSnackbar(msg) }
                        },
                    )
                }
            }
        }
    }
}

// ----- Landscape (tablet / wide-window) layout: original Row-based design -----
@Suppress("UNUSED_PARAMETER")
@Composable
private fun LandscapeLayout(
    text: LauncherStrings.Texts,
    games: List<GameEntry>,
    loading: Boolean,
    editPath: String,
    onEditPath: (String) -> Unit,
    rescan: (String) -> Unit,
    forceLandscape: Boolean,
    onForceLandscape: (Boolean) -> Unit,
    lang: String,
    onLang: (String) -> Unit,
    onSelect: (GameEntry) -> Unit,
    selectedGame: GameEntry?,
    onCloseDetail: () -> Unit,
    onLaunchGame: (GameEntry) -> Unit,
    onOpenSettings: () -> Unit,
    onOpenDiagnostics: () -> Unit,
    onLaunchOriginal: () -> Unit,
    onRequestPermission: () -> Unit,
    onExportSnack: (String) -> Unit,
) {
    val context = LocalContext.current
    Row(Modifier.fillMaxSize().padding(16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        SideBar(text, games, onOpenSettings, onOpenDiagnostics, onLaunchOriginal) { rescan(editPath) }
        Column(Modifier.weight(1f)) {
            TopControls(
                text = text,
                editPath = editPath,
                onEditPath = onEditPath,
                onRequestPermission = onRequestPermission,
                onOpenSettings = onOpenSettings,
                onScan = { rescan(editPath) },
                onReloadSaved = { val saved = LauncherPrefs.getGameRoot(context); onEditPath(saved); rescan(saved) },
                onLaunchOriginal = onLaunchOriginal,
                onExport = {
                    val file = LauncherPrefs.exportBackup(context)
                    onExportSnack("${text.exported}: ${file.absolutePath}")
                },
                forceLandscape = forceLandscape,
                onForceLandscape = onForceLandscape,
                onLangEn = { onLang(LauncherPrefs.LANG_EN) },
                onLangZh = { onLang(LauncherPrefs.LANG_ZH) },
            )
            Spacer(Modifier.height(16.dp))
            GameGrid(
                loading = loading,
                games = games,
                text = text,
                onSelect = onSelect,
                minColumnDp = 180,
                modifier = Modifier.weight(1f),
            )
        }
        selectedGame?.let { game ->
            GameDetailPanel(
                game = game,
                text = text,
                onClose = onCloseDetail,
                onLaunch = onLaunchGame,
            )
        }
    }
}

// ----- Portrait (phone) layout: stacked Column + bottom-sheet detail -----
@Suppress("UNUSED_PARAMETER")
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PortraitLayout(
    text: LauncherStrings.Texts,
    games: List<GameEntry>,
    loading: Boolean,
    editPath: String,
    onEditPath: (String) -> Unit,
    rescan: (String) -> Unit,
    forceLandscape: Boolean,
    onForceLandscape: (Boolean) -> Unit,
    lang: String,
    onLang: (String) -> Unit,
    onSelect: (GameEntry) -> Unit,
    selectedGame: GameEntry?,
    onCloseDetail: () -> Unit,
    onLaunchGame: (GameEntry) -> Unit,
    onOpenSettings: () -> Unit,
    onOpenDiagnostics: () -> Unit,
    onLaunchOriginal: () -> Unit,
    onRequestPermission: () -> Unit,
    onExportSnack: (String) -> Unit,
) {
    Column(Modifier.fillMaxSize().padding(12.dp)) {
        // Phone-friendly compact header: only the actions a user is likely
        // to need on the home screen. Path editing, language switch,
        // export-backup, launch-original, and the storage-permission
        // shortcut all live in the Settings page now.
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
            ElevatedAssistChip(onClick = { rescan(editPath) }, label = { Text(text.scan) }, leadingIcon = { Icon(Icons.Default.Refresh, null) })
            Spacer(Modifier.weight(1f))
            Text(text.forceLandscape, color = Color.White, style = MaterialTheme.typography.bodySmall)
            Switch(checked = forceLandscape, onCheckedChange = onForceLandscape)
        }
        Spacer(Modifier.height(12.dp))
        GameGrid(
            loading = loading,
            games = games,
            text = text,
            onSelect = onSelect,
            minColumnDp = 150,
            modifier = Modifier.weight(1f),
        )
    }
    if (selectedGame != null) {
        val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
        ModalBottomSheet(onDismissRequest = onCloseDetail, sheetState = sheetState, containerColor = Color(0xFF17171D)) {
            GameDetailContent(game = selectedGame, text = text, onLaunch = onLaunchGame, onClose = onCloseDetail)
        }
    }
}

@Composable
private fun SideBar(text: LauncherStrings.Texts, games: List<GameEntry>, onSettings: () -> Unit, onDiagnostics: () -> Unit, onOriginal: () -> Unit, onScan: () -> Unit) {
    val context = LocalContext.current
    Column(modifier = Modifier.width(88.dp), verticalArrangement = Arrangement.spacedBy(10.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        IconButton(onClick = onSettings) { Icon(Icons.Default.Settings, null, tint = Color.White) }
        IconButton(onClick = onDiagnostics) { Icon(Icons.Default.Info, null, tint = Color.White) }
        IconButton(onClick = onOriginal) { Icon(Icons.Default.PlayArrow, null, tint = Color.White) }
        IconButton(onClick = onScan) { Icon(Icons.Default.Refresh, null, tint = Color.White) }
        Text(text.stats, color = Color.White, style = MaterialTheme.typography.bodySmall)
        Text("${games.sumOf { LauncherPrefs.getStats(context, it.gameDir).launchCount }}", color = Color(0xFFCCCCCC), style = MaterialTheme.typography.bodySmall)
    }
}

@Suppress("UNUSED_PARAMETER")
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
    // Slimmed-down landscape header: only Scan and the landscape lock
    // toggle stay on the home screen. Storage permission, path editing,
    // Save & Scan, Reload, Launch Original, Export Backup, and the
    // language selector all moved to LauncherSettingsActivity.
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
        ElevatedAssistChip(onClick = onScan, label = { Text(text.scan) }, leadingIcon = { Icon(Icons.Default.Refresh, null) })
        Spacer(Modifier.weight(1f))
        Text(text.forceLandscape, color = Color.White)
        Switch(checked = forceLandscape, onCheckedChange = onForceLandscape)
    }
}

@Composable
private fun GameGrid(
    loading: Boolean,
    games: List<GameEntry>,
    text: LauncherStrings.Texts,
    onSelect: (GameEntry) -> Unit,
    minColumnDp: Int,
    modifier: Modifier = Modifier,
) {
    if (loading) {
        Box(modifier.fillMaxSize(), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
    } else if (games.isEmpty()) {
        Box(modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(text.noGamesFound, style = MaterialTheme.typography.headlineMedium, color = Color.White)
                Spacer(Modifier.height(8.dp))
                Text(text.emptyHint, color = Color(0xFFBBBBBB))
            }
        }
    } else {
        LazyVerticalGrid(
            columns = GridCells.Adaptive(minColumnDp.dp),
            contentPadding = PaddingValues(bottom = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
            modifier = modifier,
        ) {
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
    ElevatedCard(modifier = Modifier.fillMaxHeight().width(420.dp), shape = RoundedCornerShape(28.dp), colors = CardDefaults.elevatedCardColors(containerColor = Color(0xFF17171D))) {
        GameDetailContent(game = game, text = text, onLaunch = onLaunch, onClose = onClose)
    }
}

@Composable
private fun GameDetailContent(game: GameEntry, text: LauncherStrings.Texts, onLaunch: (GameEntry) -> Unit, onClose: () -> Unit) {
    val context = LocalContext.current
    val stats = LauncherPrefs.getStats(context, game.gameDir)
    var alias by remember(game.gameDir) { mutableStateOf(LauncherPrefs.getAlias(context, game.gameDir).orEmpty()) }
    var imagePath by remember(game.gameDir) { mutableStateOf(LauncherPrefs.getCustomImagePath(context, game.gameDir).orEmpty()) }
    var launchFile by remember(game.gameDir) { mutableStateOf(LauncherPrefs.getCustomLaunchFile(context, game.gameDir).orEmpty()) }
    var showLaunchPicker by remember { mutableStateOf(false) }
    val image = imagePath.takeIf { it.isNotBlank() } ?: game.coverPath ?: game.backgroundPath

    fun persist() {
        LauncherPrefs.setAlias(context, game.gameDir, alias)
        LauncherPrefs.setCustomImagePath(context, game.gameDir, imagePath)
        LauncherPrefs.setCustomLaunchFile(context, game.gameDir, launchFile)
    }

    Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        AsyncImage(model = image, contentDescription = null, contentScale = ContentScale.Crop, modifier = Modifier.fillMaxWidth().height(180.dp))
        Text(LauncherPrefs.displayName(context, game), color = Color.White, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
        Text(game.gameDir, color = Color(0xFFBBBBBB), style = MaterialTheme.typography.bodySmall)
        Text("UUID: ${stats.uuid}", color = Color(0xFF999999), style = MaterialTheme.typography.bodySmall)
        Text("${text.launches}: ${stats.launchCount}", color = Color.White)
        Text("${text.playTime}: ${stats.formatPlayTime()}", color = Color.White)
        OutlinedTextField(value = alias, onValueChange = { alias = it }, modifier = Modifier.fillMaxWidth(), label = { Text(text.alias) })
        OutlinedTextField(value = imagePath, onValueChange = { imagePath = it }, modifier = Modifier.fillMaxWidth(), label = { Text(text.customImagePath) })

        // Per-game launch entry. Clicking the row opens a picker that lists
        // every plausible boot file in the game directory; "Auto detect"
        // clears the override so KR2Activity falls back to its built-in
        // priority list.
        Column(modifier = Modifier.fillMaxWidth().clickable { showLaunchPicker = true }) {
            Text(text.launchFile, color = Color(0xFFCCCCCC), style = MaterialTheme.typography.bodySmall)
            Text(
                launchFile.ifBlank { text.launchFileAuto },
                color = Color.White,
                style = MaterialTheme.typography.bodyMedium,
            )
            Text(text.launchFileHint, color = Color(0xFF888888), style = MaterialTheme.typography.bodySmall)
        }

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            FilledTonalButton(onClick = { persist() }) { Text(text.save) }
            FilledTonalButton(onClick = { persist(); onLaunch(game) }) { Icon(Icons.Default.PlayArrow, null); Text(text.start) }
            FilledTonalButton(onClick = onClose) { Icon(Icons.Default.Close, null); Text("OK") }
        }
    }

    if (showLaunchPicker) {
        LaunchFilePickerDialog(
            gameDir = game.gameDir,
            current = launchFile,
            text = text,
            onPick = { picked ->
                launchFile = picked
                LauncherPrefs.setCustomLaunchFile(context, game.gameDir, picked)
                showLaunchPicker = false
            },
            onDismiss = { showLaunchPicker = false },
        )
    }
}

@Composable
private fun LaunchFilePickerDialog(
    gameDir: String,
    current: String,
    text: LauncherStrings.Texts,
    onPick: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    // Re-scan candidates on every open so the user sees newly-added files
    // without having to refresh the launcher first.
    val candidates = remember(gameDir) { GameScanner.listLaunchCandidates(File(gameDir)) }
    val scrollState = rememberScrollState()
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text.launchFile) },
        text = {
            // Cap the dialog body at ~60% screen height so a long candidate
            // list doesn't push the action buttons off-screen, then make the
            // candidate column itself scrollable. Without this the AlertDialog
            // clips overflow silently and the user can't reach entries below
            // the fold (observed on phones with deep game directories).
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(max = 420.dp)
                    .verticalScroll(scrollState),
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                Text(text.launchFileHint, style = MaterialTheme.typography.bodySmall, color = Color(0xFFBBBBBB))
                Spacer(Modifier.height(8.dp))
                LaunchFileRow(
                    label = text.launchFileAuto,
                    sub = null,
                    selected = current.isBlank(),
                    onClick = { onPick("") },
                )
                candidates.forEach { f ->
                    LaunchFileRow(
                        label = f.name,
                        sub = f.absolutePath,
                        selected = f.absolutePath == current,
                        onClick = { onPick(f.absolutePath) },
                    )
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text(text.close) }
        },
    )
}

@Composable
private fun LaunchFileRow(label: String, sub: String?, selected: Boolean, onClick: () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        RadioButton(selected = selected, onClick = onClick)
        Column {
            Text(label, style = MaterialTheme.typography.bodyMedium)
            if (!sub.isNullOrBlank()) {
                Text(sub, style = MaterialTheme.typography.bodySmall, color = Color(0xFF888888))
            }
        }
    }
}
