package org.github.krkr2

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Language
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Update
import androidx.compose.material.icons.filled.Upload
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ElevatedAssistChip
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.graphics.drawable.toBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

class LauncherSettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        setContent {
            LauncherTheme {
                SettingsScreen(
                    onBack = { finish() },
                    onOpenRenderSettings = { startActivity(Intent(this, RenderSettingsActivity::class.java)) },
                    onOpenDiagnostics = { startActivity(Intent(this, DiagnosticsActivity::class.java)) },
                    onLaunchOriginal = {
                        val intent = Intent(this, MainActivity::class.java)
                        intent.flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
                        startActivity(intent)
                        finish()
                    },
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
    }
}

private enum class SettingsDest { Library, Display, Engine, Tools, About }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SettingsScreen(
    onBack: () -> Unit,
    onOpenRenderSettings: () -> Unit,
    onOpenDiagnostics: () -> Unit,
    onLaunchOriginal: () -> Unit,
) {
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }
    var lang by remember { mutableStateOf(LauncherPrefs.getLanguage(context)) }
    val text = if (lang == LauncherPrefs.LANG_ZH) LauncherStrings.zh else LauncherStrings.en
    var pathInput by remember { mutableStateOf(LauncherPrefs.getGameRoot(context)) }
    var scanDepth by remember { mutableStateOf(LauncherPrefs.getScanDepth(context)) }
    var statusLine by remember { mutableStateOf("") }
    var dest by remember { mutableStateOf(SettingsDest.Library) }
    val landscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
    val compact = landscape && configuration.screenHeightDp < 520
    val pad = if (compact) 8.dp else 16.dp
    val gap = if (compact) 8.dp else 16.dp

    fun saveAndScan() {
        val normalized = pathInput.trim().ifBlank { LauncherPrefs.DEFAULT_GAME_ROOT }
        LauncherPrefs.setGameRoot(context, normalized)
        scope.launch {
            val games = withContext(Dispatchers.IO) { GameScanner.scan(java.io.File(normalized), maxDepth = scanDepth) }
            statusLine = "${text.scan}: ${games.size}"
            snackbarHostState.showSnackbar(statusLine)
        }
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbarHostState) },
        topBar = {
            TopAppBar(
                title = { Text(text.settings, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = text.close) } },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.surface),
            )
        },
        bottomBar = {
            if (!landscape) SettingsBottomBar(dest, { dest = it }, text)
        },
    ) { padding ->
        if (landscape) {
            Row(Modifier.fillMaxSize().padding(padding)) {
                SettingsRail(dest, { dest = it }, text)
                Column(
                    Modifier.width(if (compact) 248.dp else 300.dp).fillMaxHeight().padding(pad),
                    verticalArrangement = Arrangement.spacedBy(gap),
                ) {
                    SettingsHero(text, statusLine, compact, onPrimary = { saveAndScan() }, onSecondary = {
                        scope.launch {
                            val games = withContext(Dispatchers.IO) { GameScanner.scan(java.io.File(LauncherPrefs.getGameRoot(context)), maxDepth = scanDepth) }
                            statusLine = "${text.scan}: ${games.size}"
                            snackbarHostState.showSnackbar(statusLine)
                        }
                    })
                    QuickStatusCard(text, pathInput, scanDepth, compact)
                }
                Box(Modifier.weight(1f).fillMaxHeight().padding(end = pad, top = pad, bottom = pad)) {
                    SettingsContent(
                        dest = dest,
                        text = text,
                        compact = compact,
                        pathInput = pathInput,
                        onPathChange = { pathInput = it },
                        scanDepth = scanDepth,
                        onScanDepthChange = { scanDepth = it },
                        statusLine = statusLine,
                        onSaveAndScan = { saveAndScan() },
                        onRefresh = {
                            scope.launch {
                                val games = withContext(Dispatchers.IO) { GameScanner.scan(java.io.File(LauncherPrefs.getGameRoot(context)), maxDepth = scanDepth) }
                                statusLine = "${text.scan}: ${games.size}"
                                snackbarHostState.showSnackbar(statusLine)
                            }
                        },
                        onGrantStorage = {
                            val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.fromParts("package", context.packageName, null))
                            try { context.startActivity(intent) } catch (e: ActivityNotFoundException) { scope.launch { snackbarHostState.showSnackbar(e.message ?: "") } }
                        },
                        onLangChange = { newLang -> LauncherPrefs.setLanguage(context, newLang); lang = newLang },
                        onOpenRenderSettings = onOpenRenderSettings,
                        onOpenDiagnostics = onOpenDiagnostics,
                        onLaunchOriginal = onLaunchOriginal,
                        onExportBackup = {
                            val out = LauncherPrefs.exportBackup(context)
                            statusLine = "${text.exported}: ${out.absolutePath}"
                            scope.launch { snackbarHostState.showSnackbar("${text.exported}: ${out.name}") }
                        },
                        onCopy = { value -> copyToClipboard(context, value); scope.launch { snackbarHostState.showSnackbar(text.aboutCopiedUrl) } },
                    )
                }
            }
        } else {
            SettingsContent(
                dest = dest,
                text = text,
                compact = false,
                pathInput = pathInput,
                onPathChange = { pathInput = it },
                scanDepth = scanDepth,
                onScanDepthChange = { scanDepth = it },
                statusLine = statusLine,
                onSaveAndScan = { saveAndScan() },
                onRefresh = { saveAndScan() },
                onGrantStorage = {
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.fromParts("package", context.packageName, null))
                    context.startActivity(intent)
                },
                onLangChange = { newLang -> LauncherPrefs.setLanguage(context, newLang); lang = newLang },
                onOpenRenderSettings = onOpenRenderSettings,
                onOpenDiagnostics = onOpenDiagnostics,
                onLaunchOriginal = onLaunchOriginal,
                onExportBackup = {
                    val out = LauncherPrefs.exportBackup(context)
                    statusLine = "${text.exported}: ${out.absolutePath}"
                },
                onCopy = { value -> copyToClipboard(context, value) },
                modifier = Modifier.fillMaxSize().padding(padding).padding(pad),
            )
        }
    }
}

@Composable
private fun SettingsRail(dest: SettingsDest, onSelect: (SettingsDest) -> Unit, text: LauncherStrings.Texts) {
    NavigationRail(containerColor = MaterialTheme.colorScheme.surfaceContainer) {
        RailItem(dest, SettingsDest.Library, onSelect, Icons.Default.FolderOpen, text.settingsLibrary)
        RailItem(dest, SettingsDest.Display, onSelect, Icons.Default.Language, text.settingsDisplay)
        RailItem(dest, SettingsDest.Engine, onSelect, Icons.Default.Tune, text.settingsEngine)
        RailItem(dest, SettingsDest.Tools, onSelect, Icons.Default.BugReport, text.settingsTools)
        RailItem(dest, SettingsDest.About, onSelect, Icons.Default.Info, text.settingsAbout)
    }
}

@Composable
private fun RailItem(current: SettingsDest, item: SettingsDest, onSelect: (SettingsDest) -> Unit, icon: ImageVector, label: String) {
    NavigationRailItem(selected = current == item, onClick = { onSelect(item) }, icon = { Icon(icon, null) }, label = { Text(label, maxLines = 1) })
}

@Composable
private fun SettingsBottomBar(dest: SettingsDest, onSelect: (SettingsDest) -> Unit, text: LauncherStrings.Texts) {
    NavigationBar {
        NavigationBarItem(selected = dest == SettingsDest.Library, onClick = { onSelect(SettingsDest.Library) }, icon = { Icon(Icons.Default.FolderOpen, null) }, label = { Text(text.settingsLibrary) })
        NavigationBarItem(selected = dest == SettingsDest.Display, onClick = { onSelect(SettingsDest.Display) }, icon = { Icon(Icons.Default.Language, null) }, label = { Text(text.settingsDisplay) })
        NavigationBarItem(selected = dest == SettingsDest.Engine, onClick = { onSelect(SettingsDest.Engine) }, icon = { Icon(Icons.Default.Tune, null) }, label = { Text(text.settingsEngine) })
        NavigationBarItem(selected = dest == SettingsDest.Tools, onClick = { onSelect(SettingsDest.Tools) }, icon = { Icon(Icons.Default.BugReport, null) }, label = { Text(text.settingsTools) })
    }
}

@Composable
private fun SettingsHero(text: LauncherStrings.Texts, statusLine: String, compact: Boolean, onPrimary: () -> Unit, onSecondary: () -> Unit) {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer), shape = RoundedCornerShape(if (compact) 18.dp else 28.dp)) {
        Column(Modifier.fillMaxWidth().padding(if (compact) 12.dp else 16.dp), verticalArrangement = Arrangement.spacedBy(if (compact) 8.dp else 12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Surface(color = MaterialTheme.colorScheme.primary, shape = RoundedCornerShape(14.dp)) {
                    Box(Modifier.size(if (compact) 40.dp else 48.dp), contentAlignment = Alignment.Center) { Icon(Icons.Default.Settings, null, tint = MaterialTheme.colorScheme.onPrimary) }
                }
                Column(Modifier.weight(1f)) {
                    Text(text.settings, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold, color = MaterialTheme.colorScheme.onPrimaryContainer)
                    Text(text.renderSettingsHint, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.78f), maxLines = if (compact) 2 else 3, overflow = TextOverflow.Ellipsis)
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilledTonalButton(onClick = onPrimary) { Icon(Icons.Default.Save, null); Spacer(Modifier.width(6.dp)); Text(text.saveAndScan) }
                TextButton(onClick = onSecondary) { Icon(Icons.Default.Refresh, null); Spacer(Modifier.width(6.dp)); Text(text.refresh) }
            }
            if (statusLine.isNotBlank()) Text(statusLine, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onPrimaryContainer)
        }
    }
}

@Composable
private fun QuickStatusCard(text: LauncherStrings.Texts, path: String, depth: Int, compact: Boolean) {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHigh), shape = RoundedCornerShape(18.dp)) {
        Column(Modifier.fillMaxWidth().padding(if (compact) 10.dp else 14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(text.settingsLibrary, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
            Text(path, maxLines = 2, overflow = TextOverflow.Ellipsis, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
            Text("${text.scanDepth}: $depth/10", color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun SettingsContent(
    dest: SettingsDest,
    text: LauncherStrings.Texts,
    compact: Boolean,
    pathInput: String,
    onPathChange: (String) -> Unit,
    scanDepth: Int,
    onScanDepthChange: (Int) -> Unit,
    statusLine: String,
    onSaveAndScan: () -> Unit,
    onRefresh: () -> Unit,
    onGrantStorage: () -> Unit,
    onLangChange: (String) -> Unit,
    onOpenRenderSettings: () -> Unit,
    onOpenDiagnostics: () -> Unit,
    onLaunchOriginal: () -> Unit,
    onExportBackup: () -> Unit,
    onCopy: (String) -> Unit,
    modifier: Modifier = Modifier.fillMaxSize(),
) {
    val scroll = rememberScrollState()
    Column(modifier.verticalScroll(scroll), verticalArrangement = Arrangement.spacedBy(if (compact) 8.dp else 12.dp)) {
        when (dest) {
            SettingsDest.Library -> LibrarySettings(text, compact, pathInput, onPathChange, scanDepth, onScanDepthChange, statusLine, onSaveAndScan, onRefresh, onGrantStorage)
            SettingsDest.Display -> DisplaySettings(text, compact, onLangChange)
            SettingsDest.Engine -> EngineSettings(text, compact, onOpenRenderSettings)
            SettingsDest.Tools -> ToolsSettings(text, compact, onOpenDiagnostics, onLaunchOriginal, onExportBackup)
            SettingsDest.About -> AboutSettings(text, compact, onCopy)
        }
    }
}

@Composable
private fun LibrarySettings(text: LauncherStrings.Texts, compact: Boolean, pathInput: String, onPathChange: (String) -> Unit, scanDepth: Int, onScanDepthChange: (Int) -> Unit, statusLine: String, onSaveAndScan: () -> Unit, onRefresh: () -> Unit, onGrantStorage: () -> Unit) {
    SettingsPanel(text.settingsLibrary, Icons.Default.FolderOpen, compact) {
        OutlinedTextField(value = pathInput, onValueChange = onPathChange, label = { Text(text.gameRootPath) }, singleLine = true, modifier = Modifier.fillMaxWidth())
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            PrimaryChip(text.saveAndScan, Icons.Default.Save, onSaveAndScan)
            PrimaryChip(text.refresh, Icons.Default.Refresh, onRefresh)
        }
        Slider(value = scanDepth.toFloat(), onValueChange = { onScanDepthChange(it.toInt().coerceIn(1, 10)) }, valueRange = 1f..10f, steps = 8, onValueChangeFinished = { })
        Text("${text.scanDepth}: $scanDepth/10", color = MaterialTheme.colorScheme.onSurfaceVariant)
        if (statusLine.isNotBlank()) Text(statusLine, color = MaterialTheme.colorScheme.primary, style = MaterialTheme.typography.bodySmall)
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        RowSetting(Icons.Default.FolderOpen, text.grantStorage, "Allow all files access for scanning games.", onClick = onGrantStorage)
    }
}

@Composable
private fun DisplaySettings(text: LauncherStrings.Texts, compact: Boolean, onLangChange: (String) -> Unit) {
    SettingsPanel(text.settingsDisplay, Icons.Default.Language, compact) {
        RowSetting(Icons.Default.Language, text.language, text.languageHint, trailing = {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(onClick = { onLangChange(LauncherPrefs.LANG_EN) }) { Text("EN") }
                TextButton(onClick = { onLangChange(LauncherPrefs.LANG_ZH) }) { Text("中") }
            }
        })
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        RowSetting(Icons.Default.Settings, text.forceLandscape, "Launcher and engine screens prefer landscape orientation.", showChevron = false)
    }
}

@Composable
private fun EngineSettings(text: LauncherStrings.Texts, compact: Boolean, onOpenRenderSettings: () -> Unit) {
    SettingsPanel(text.settingsEngine, Icons.Default.Tune, compact) {
        RowSetting(Icons.Default.Tune, text.renderSettings, text.renderSettingsHint, onClick = onOpenRenderSettings)
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        RowSetting(Icons.Default.Settings, text.gameOverride, text.gameOverrideHint, showChevron = false)
    }
}

@Composable
private fun ToolsSettings(text: LauncherStrings.Texts, compact: Boolean, onOpenDiagnostics: () -> Unit, onLaunchOriginal: () -> Unit, onExportBackup: () -> Unit) {
    SettingsPanel(text.settingsTools, Icons.Default.BugReport, compact) {
        RowSetting(Icons.Default.BugReport, text.diagnostics, "Inspect launcher and engine logs.", onClick = onOpenDiagnostics)
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        RowSetting(Icons.AutoMirrored.Filled.OpenInNew, text.launchOriginal, "Open the engine without a selected game.", onClick = onLaunchOriginal)
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        RowSetting(Icons.Default.Upload, text.exportBackup, "Export launcher preferences to a backup file.", onClick = onExportBackup)
    }
}

@Composable
private fun AboutSettings(text: LauncherStrings.Texts, compact: Boolean, onCopy: (String) -> Unit) {
    val context = LocalContext.current
    val pInfo = remember { runCatching { context.packageManager.getPackageInfo(context.packageName, 0) }.getOrNull() }
    val appIcon = remember { runCatching { context.packageManager.getApplicationIcon(context.packageName) }.getOrNull() }
    val versionName = pInfo?.versionName ?: "—"
    val versionCode = pInfo?.let {
        @Suppress("DEPRECATION")
        if (android.os.Build.VERSION.SDK_INT >= 28) it.longVersionCode else it.versionCode.toLong()
    } ?: 0L
    var checking by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    SettingsPanel(text.settingsAbout, Icons.Default.Info, compact) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Surface(color = MaterialTheme.colorScheme.primaryContainer, shape = RoundedCornerShape(18.dp)) {
                Box(Modifier.size(if (compact) 48.dp else 56.dp), contentAlignment = Alignment.Center) {
                    if (appIcon != null) Image(bitmap = appIcon.toBitmap(56, 56).asImageBitmap(), contentDescription = text.aboutTitle, modifier = Modifier.size(if (compact) 42.dp else 50.dp))
                    else Icon(Icons.Default.Info, null, tint = MaterialTheme.colorScheme.onPrimaryContainer)
                }
            }
            Column(Modifier.weight(1f)) {
                Text(text.aboutTitle, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                Text(text.aboutOpenSourceUrl, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
        RowSetting(Icons.Default.Info, text.appVersion, "$versionName (#$versionCode)", showChevron = false)
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        RowSetting(Icons.Default.Code, text.aboutOpenSource, text.aboutOpenSourceUrl, onClick = { onCopy(text.aboutOpenSourceUrl) })
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        RowSetting(Icons.Default.Update, if (checking) text.aboutCheckingUpdate else text.aboutCheckUpdate, "Check whether a newer release exists.", onClick = {
            if (checking) return@RowSetting
            checking = true
            scope.launch {
                val result = checkUpdate(versionName)
                checking = false
                snackbarHostState.showSnackbar(
                    when (result) {
                        UpdateResult.UpToDate -> text.aboutAlreadyLatest
                        is UpdateResult.NewVersion -> "${text.aboutNewVersion} ${result.tag}"
                        UpdateResult.Failed -> text.aboutUpdateFailed
                    }
                )
            }
        })
        SnackbarHost(snackbarHostState)
    }
}

@Composable
private fun SettingsPanel(title: String, icon: ImageVector, compact: Boolean, content: @Composable ColumnScope.() -> Unit) {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHigh), shape = RoundedCornerShape(if (compact) 18.dp else 24.dp)) {
        Column(Modifier.fillMaxWidth().padding(if (compact) 12.dp else 16.dp), verticalArrangement = Arrangement.spacedBy(if (compact) 8.dp else 12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Surface(color = MaterialTheme.colorScheme.secondaryContainer, shape = RoundedCornerShape(14.dp)) {
                    Box(Modifier.size(40.dp), contentAlignment = Alignment.Center) { Icon(icon, null, tint = MaterialTheme.colorScheme.onSecondaryContainer) }
                }
                Text(title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
            }
            content()
        }
    }
}

@Composable
private fun RowSetting(icon: ImageVector, title: String, subtitle: String? = null, onClick: (() -> Unit)? = null, showChevron: Boolean = onClick != null, trailing: (@Composable () -> Unit)? = null) {
    Surface(onClick = { onClick?.invoke() }, enabled = onClick != null, color = Color.Transparent, modifier = Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Surface(color = MaterialTheme.colorScheme.secondaryContainer, shape = RoundedCornerShape(12.dp)) {
                Box(Modifier.size(40.dp), contentAlignment = Alignment.Center) { Icon(icon, null, tint = MaterialTheme.colorScheme.onSecondaryContainer) }
            }
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text(title, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium)
                if (subtitle != null) Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2, overflow = TextOverflow.Ellipsis)
            }
            if (trailing != null) trailing() else if (showChevron) Text("›", style = MaterialTheme.typography.titleLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun PrimaryChip(text: String, icon: ImageVector, onClick: () -> Unit) {
    ElevatedAssistChip(onClick = onClick, label = { Text(text) }, leadingIcon = { Icon(icon, null, modifier = Modifier.size(16.dp)) })
}

private sealed class UpdateResult {
    data object UpToDate : UpdateResult()
    data class NewVersion(val tag: String) : UpdateResult()
    data object Failed : UpdateResult()
}

private suspend fun checkUpdate(currentVersion: String): UpdateResult = withContext(Dispatchers.IO) {
    runCatching {
        val url = URL("https://api.github.com/repos/xiaocongyu66/krkr2/releases/latest")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            connectTimeout = 8000
            readTimeout = 8000
            requestMethod = "GET"
            setRequestProperty("Accept", "application/vnd.github+json")
            setRequestProperty("User-Agent", "krkr2-launcher/$currentVersion")
        }
        try {
            if (conn.responseCode != 200) return@runCatching UpdateResult.Failed
            val body = conn.inputStream.bufferedReader().use { it.readText() }
            val tag = JSONObject(body).optString("tag_name").trim()
            if (tag.isEmpty()) UpdateResult.Failed
            else if (tag.equals(currentVersion, ignoreCase = true) || tag.equals("v$currentVersion", ignoreCase = true)) UpdateResult.UpToDate
            else UpdateResult.NewVersion(tag)
        } finally {
            conn.disconnect()
        }
    }.getOrElse { UpdateResult.Failed }
}

private fun copyToClipboard(context: Context, text: String) {
    val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
    cm?.setPrimaryClip(ClipData.newPlainText("krkr2", text))
}
