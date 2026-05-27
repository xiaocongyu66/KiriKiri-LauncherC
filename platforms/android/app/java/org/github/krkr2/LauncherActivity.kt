package org.github.krkr2

import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.os.Bundle
import android.provider.Settings
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.GridView
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ElevatedAssistChip
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class LauncherActivity : AppCompatActivity() {
    private var refreshHome: (() -> Unit)? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        applyLauncherOrientation()
        setContent {
            LauncherTheme {
                LauncherScreen(
                    onOpenSettings = { startActivity(Intent(this, LauncherSettingsActivity::class.java)) },
                    onOpenDiagnostics = { startActivity(Intent(this, DiagnosticsActivity::class.java)) },
                    onLaunchGame = { game -> startGame(game.gameDir, game.title) },
                    onRequestPermission = { requestStoragePermission() },
                    onRegisterRefresh = { refreshHome = it },
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        applyLauncherOrientation()
        refreshHome?.invoke()
    }

    private fun applyLauncherOrientation() {
        requestedOrientation = if (ForceLandscapeHelper.isTabletClass(this)) {
            ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        } else {
            ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        }
    }

    private fun startGame(gameDir: String, title: String) {
        val intent = Intent(this, MainActivity::class.java)
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
        intent.putExtra(MainActivity.EXTRA_GAME_DIR, gameDir)
        intent.putExtra(MainActivity.EXTRA_GAME_TITLE, title)
        LauncherPrefs.getCustomLaunchFile(this, gameDir)
            ?.takeIf { it.isNotBlank() }
            ?.let { path ->
                val f = File(path)
                if (f.isFile && f.canRead()) intent.putExtra(MainActivity.EXTRA_LAUNCH_FILE, f.absolutePath)
            }
        startActivity(intent)
    }

    private fun startOriginal() {
        val intent = Intent(this, MainActivity::class.java)
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
        val root = LauncherPrefs.getGameRoot(this)
        intent.putExtra(MainActivity.EXTRA_GAME_DIR, root)
        LauncherPrefs.getCustomLaunchFile(this, root)
            ?.takeIf { it.isNotBlank() }
            ?.let { path ->
                val f = File(path)
                if (f.isFile && f.canRead()) intent.putExtra(MainActivity.EXTRA_LAUNCH_FILE, f.absolutePath)
            }
        startActivity(intent)
    }

    private fun requestStoragePermission() {
        val intent = Intent(
            Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
            android.net.Uri.fromParts("package", packageName, null)
        )
        startActivity(intent)
    }
}

private enum class LauncherDest { Library, Settings, Tools }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun LauncherScreen(
    onOpenSettings: () -> Unit,
    onOpenDiagnostics: () -> Unit,
    onLaunchGame: (GameEntry) -> Unit,
    onRequestPermission: () -> Unit,
    onRegisterRefresh: ((() -> Unit)?) -> Unit,
) {
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }
    val cachedSeed = remember { GameCache.load(context) }
    var loading by remember { mutableStateOf(cachedSeed.isEmpty()) }
    var games by remember { mutableStateOf(cachedSeed) }
    var scanProgressPath by remember { mutableStateOf("") }
    var rootPath by remember { mutableStateOf(LauncherPrefs.getGameRoot(context)) }
    var editPath by remember { mutableStateOf(rootPath) }
    var lang by remember { mutableStateOf(LauncherPrefs.getLanguage(context)) }
    var selectedGame by remember { mutableStateOf<GameEntry?>(null) }
    var currentDest by remember { mutableStateOf(LauncherDest.Library) }
    var showRootSheet by rememberSaveable { mutableStateOf(false) }
    val text: LauncherStrings.Texts = if (lang == LauncherPrefs.LANG_ZH) LauncherStrings.zh else LauncherStrings.en

    fun rescan(path: String) {
        val normalized = path.trim().ifBlank { LauncherPrefs.DEFAULT_GAME_ROOT }
        rootPath = normalized
        editPath = normalized
        selectedGame = null
        LauncherPrefs.setGameRoot(context, normalized)
        loading = true
        scope.launch {
            val depth = LauncherPrefs.getScanDepth(context)
            val fresh = withContext(Dispatchers.IO) {
                GameScanner.scan(File(normalized), maxDepth = depth, onProgress = { scanProgressPath = it })
            }
            games = fresh
            loading = false
            scanProgressPath = ""
            withContext(Dispatchers.IO) {
                GameCache.setCachedRoot(context, normalized)
                GameCache.save(context, fresh)
            }
        }
    }

    fun refreshFromPrefs() {
        val newLang = LauncherPrefs.getLanguage(context)
        val newRoot = LauncherPrefs.getGameRoot(context)
        lang = newLang
        if (newRoot != rootPath) {
            rescan(newRoot)
        } else {
            rootPath = newRoot
            editPath = newRoot
            if (games.isEmpty() && !loading) {
                rescan(newRoot)
            }
        }
    }

    DisposableEffect(Unit) {
        onRegisterRefresh { refreshFromPrefs() }
        onDispose { onRegisterRefresh(null) }
    }

    LaunchedEffect(rootPath) {
        val cachedRoot = withContext(Dispatchers.IO) { GameCache.getCachedRoot(context) }
        val haveValidCache = games.isNotEmpty() && cachedRoot == rootPath
        if (haveValidCache) {
            val verified = withContext(Dispatchers.IO) { games.filter { GameCache.checkEntry(it) == GameCache.EntryStatus.VALID } }
            if (verified.size != games.size) {
                games = verified
                withContext(Dispatchers.IO) { GameCache.save(context, verified) }
            }
        } else {
            loading = true
        }
        val depth = LauncherPrefs.getScanDepth(context)
        val fresh = withContext(Dispatchers.IO) {
            GameScanner.scan(File(rootPath), maxDepth = depth, onProgress = { scanProgressPath = it })
        }
        games = fresh
        loading = false
        scanProgressPath = ""
        withContext(Dispatchers.IO) {
            GameCache.setCachedRoot(context, rootPath)
            GameCache.save(context, fresh)
        }
    }

    BoxWithConstraints {
        val landscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        val compactHeight = maxHeight < 520.dp
        val compactWidth = maxWidth < 600.dp
        val compactHome = compactWidth || compactHeight
        val expandedLayout = maxWidth >= 840.dp || (landscape && maxWidth >= 600.dp)
        val contentPadding = if (compactHome) 8.dp else 16.dp
        val contentGap = if (compactHome) 8.dp else 16.dp
        Scaffold(
            snackbarHost = { SnackbarHost(snackbarHostState) },
            topBar = {
                TopAppBar(
                    title = {
                        Column {
                            Text(text.appName)
                            Text(
                                if (loading) text.scanning else "${games.size} ${text.running}",
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    },
                    actions = {
                        IconButton(onClick = { showRootSheet = true }) { Icon(Icons.Default.Add, contentDescription = text.gameRootPath) }
                        IconButton(onClick = { rescan(editPath) }) { Icon(Icons.Default.Refresh, contentDescription = text.refresh) }
                        IconButton(onClick = onOpenSettings) { Icon(Icons.Default.Settings, contentDescription = text.settings) }
                    },
                    colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.surface),
                )
            },
            bottomBar = {
                if (!expandedLayout) {
                    NavigationBar {
                        NavigationBarItem(selected = currentDest == LauncherDest.Library, onClick = { currentDest = LauncherDest.Library }, icon = { Icon(Icons.Default.Home, null) }, label = { Text(text.home) })
                        NavigationBarItem(selected = currentDest == LauncherDest.Settings, onClick = { currentDest = LauncherDest.Settings; onOpenSettings() }, icon = { Icon(Icons.Default.Settings, null) }, label = { Text(text.settings) })
                        NavigationBarItem(selected = currentDest == LauncherDest.Tools, onClick = { currentDest = LauncherDest.Tools; onOpenDiagnostics() }, icon = { Icon(Icons.Default.Info, null) }, label = { Text(text.tools) })
                    }
                }
            },
        ) { padding ->
            if (expandedLayout) {
                Row(Modifier.fillMaxSize().padding(padding)) {
                    NavigationRail {
                        NavigationRailItem(selected = currentDest == LauncherDest.Library, onClick = { currentDest = LauncherDest.Library }, icon = { Icon(Icons.Default.Home, null) }, label = { Text(text.home) })
                        NavigationRailItem(selected = currentDest == LauncherDest.Settings, onClick = { currentDest = LauncherDest.Settings; onOpenSettings() }, icon = { Icon(Icons.Default.Settings, null) }, label = { Text(text.settings) })
                        NavigationRailItem(selected = currentDest == LauncherDest.Tools, onClick = { currentDest = LauncherDest.Tools; onOpenDiagnostics() }, icon = { Icon(Icons.Default.Info, null) }, label = { Text(text.tools) })
                    }
                    Column(Modifier.weight(1f).fillMaxSize().padding(contentPadding), verticalArrangement = Arrangement.spacedBy(contentGap)) {
                        LauncherHero(
                            rootPath = rootPath,
                            onOpenPermission = onRequestPermission,
                            scanProgressPath = scanProgressPath,
                            loading = loading,
                            gamesCount = games.size,
                            text = text,
                            compact = compactHome,
                        )
                        Row(Modifier.weight(1f), horizontalArrangement = Arrangement.spacedBy(contentGap)) {
                            Box(Modifier.weight(1.2f).fillMaxHeight()) { GameGrid(games, true, { selectedGame = it }, onLaunchGame, loading, text) }
                            Box(Modifier.weight(0.8f).fillMaxHeight()) {
                                selectedGame?.let { game ->
                                    GameDetailPane(game, { onLaunchGame(game) }, { selectedGame = null }, text)
                                } ?: EmptyDetailPane(text)
                            }
                        }
                    }
                }
            } else {
                Column(Modifier.fillMaxSize().padding(padding).padding(contentPadding), verticalArrangement = Arrangement.spacedBy(contentGap)) {
                    LauncherHero(
                        rootPath = rootPath,
                        onOpenPermission = onRequestPermission,
                        scanProgressPath = scanProgressPath,
                        loading = loading,
                        gamesCount = games.size,
                        text = text,
                        compact = compactHome,
                    )
                    Box(Modifier.weight(1f).fillMaxWidth()) {
                        GameGrid(games, false, { selectedGame = it }, onLaunchGame, loading, text)
                    }
                }
            }
        }

        if (showRootSheet) {
            val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
            ModalBottomSheet(onDismissRequest = { showRootSheet = false }, sheetState = sheetState) {
                RootConfigSheet(
                    rootPath = editPath,
                    onRootPathChange = { editPath = it },
                    onSave = {
                        rescan(editPath)
                        showRootSheet = false
                    },
                    onRefresh = { rescan(editPath) },
                    onGrantStorage = onRequestPermission,
                    onClose = { showRootSheet = false },
                    text = text,
                )
            }
        }

        if (selectedGame != null && !expandedLayout) {
            val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
            ModalBottomSheet(onDismissRequest = { selectedGame = null }, sheetState = sheetState) {
                val game = selectedGame!!
                GameDetailPane(game, { onLaunchGame(game) }, { selectedGame = null }, text)
            }
        }
    }
}

@Composable
private fun LauncherHero(
    rootPath: String,
    onOpenPermission: () -> Unit,
    scanProgressPath: String,
    loading: Boolean,
    gamesCount: Int,
    text: LauncherStrings.Texts,
    compact: Boolean,
) {
    ElevatedCard(colors = CardDefaults.elevatedCardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHighest)) {
            Column(Modifier.fillMaxWidth().padding(if (compact) 8.dp else 12.dp), verticalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 8.dp)) {
                Text(text.appName, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    ElevatedAssistChip(onClick = onOpenPermission, label = { Text(text.grantStorage) }, leadingIcon = { Icon(Icons.Default.FolderOpen, null) })
                }
                if (loading) {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        CircularProgressIndicator(modifier = Modifier.width(20.dp), strokeWidth = 2.dp)
                        Text(if (scanProgressPath.isBlank()) text.scanning else scanProgressPath, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }
    }
}

@Composable
private fun RootConfigSheet(
    rootPath: String,
    onRootPathChange: (String) -> Unit,
    onSave: () -> Unit,
    onRefresh: () -> Unit,
    onGrantStorage: () -> Unit,
    onClose: () -> Unit,
    text: LauncherStrings.Texts,
) {
    Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(text.gameRootPath, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
        OutlinedTextField(
            value = rootPath,
            onValueChange = onRootPathChange,
            modifier = Modifier.fillMaxWidth(),
            label = { Text(text.gameRootPath) },
            singleLine = true,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ElevatedAssistChip(onClick = onGrantStorage, label = { Text(text.grantStorage) }, leadingIcon = { Icon(Icons.Default.FolderOpen, null) })
            ElevatedAssistChip(onClick = onRefresh, label = { Text(text.refresh) }, leadingIcon = { Icon(Icons.Default.Refresh, null) })
        }
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
            TextButton(onClick = onClose, modifier = Modifier.weight(1f)) { Text(text.close) }
            FilledTonalButton(onClick = onSave, modifier = Modifier.weight(1f)) { Text(text.saveAndScan) }
        }
    }
}

@Composable
private fun GameGrid(
    games: List<GameEntry>,
    expanded: Boolean,
    onGameClick: (GameEntry) -> Unit,
    onLaunchGame: (GameEntry) -> Unit,
    loading: Boolean,
    text: LauncherStrings.Texts,
) {
    if (games.isEmpty()) {
        EmptyState(loading, text)
        return
    }
    val columns = if (expanded) 4 else 2
    LazyVerticalGrid(
        columns = GridCells.Fixed(columns),
        contentPadding = PaddingValues(bottom = 24.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        modifier = Modifier.fillMaxSize(),
    ) {
        items(games) { game -> GameCard(game, { onGameClick(game) }, { onLaunchGame(game) }, text) }
    }
}

@Composable
private fun GameCard(game: GameEntry, onClick: () -> Unit, onLaunch: () -> Unit, text: LauncherStrings.Texts) {
    ElevatedCard(
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick),
        colors = CardDefaults.elevatedCardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow),
    ) {
        Column(Modifier.fillMaxWidth()) {
            Box(
                Modifier.fillMaxWidth().aspectRatio(1.78f).background(
                    Brush.verticalGradient(listOf(MaterialTheme.colorScheme.primary.copy(alpha = 0.25f), MaterialTheme.colorScheme.surfaceVariant))
                )
            ) {
                val image = game.iconPath?.takeIf { it.isNotBlank() }
                    ?: game.bannerPath?.takeIf { it.isNotBlank() }
                    ?: game.thumbnailPath?.takeIf { it.isNotBlank() }
                if (image != null) {
                    AsyncImage(model = File(image), contentDescription = game.title, contentScale = ContentScale.Crop, modifier = Modifier.fillMaxSize())
                } else {
                    Icon(Icons.Default.GridView, null, tint = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.align(Alignment.Center))
                }
            }
            Column(Modifier.fillMaxWidth().padding(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(game.title, style = MaterialTheme.typography.titleMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(game.gameDir, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    TextButton(onClick = onLaunch) { Icon(Icons.Default.PlayArrow, null); Spacer(Modifier.width(4.dp)); Text(text.play) }
                    TextButton(onClick = onClick) { Icon(Icons.Default.Info, null); Spacer(Modifier.width(4.dp)); Text(text.info) }
                }
            }
        }
    }
}

@Composable
private fun GameDetailPane(game: GameEntry, onLaunch: () -> Unit, onClose: () -> Unit, text: LauncherStrings.Texts) {
    ElevatedCard(colors = CardDefaults.elevatedCardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHighest), modifier = Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().padding(10.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                Text(game.title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                IconButton(onClick = onClose) { Icon(Icons.Default.Close, null) }
            }
            Text(game.gameDir, color = MaterialTheme.colorScheme.onSurfaceVariant)
            game.iconPath?.let { path ->
                AsyncImage(model = File(path), contentDescription = game.title, modifier = Modifier.fillMaxWidth().height(120.dp).clip(RoundedCornerShape(16.dp)), contentScale = ContentScale.Crop)
            }
            Text(game.description?.takeIf { it.isNotBlank() } ?: text.noDescription, color = MaterialTheme.colorScheme.onSurfaceVariant)
            FilledTonalButton(onClick = onLaunch, modifier = Modifier.fillMaxWidth()) { Icon(Icons.Default.PlayArrow, null); Spacer(Modifier.width(8.dp)); Text(text.launch) }
        }
    }
}

@Composable
private fun EmptyDetailPane(text: LauncherStrings.Texts) {
    Surface(color = MaterialTheme.colorScheme.surfaceContainerHigh, modifier = Modifier.fillMaxSize()) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Icon(Icons.Default.Info, null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
                Text(text.noGameSelected, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

@Composable
private fun EmptyState(loading: Boolean, text: LauncherStrings.Texts) {
    Surface(color = MaterialTheme.colorScheme.surfaceContainerLow, modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
            if (loading) CircularProgressIndicator() else Icon(Icons.Default.FolderOpen, null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(if (loading) text.scanning else text.noGamesFound, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}