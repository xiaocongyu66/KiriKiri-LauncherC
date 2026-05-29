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
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.size
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
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
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
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

class LauncherSettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        applyLauncherOrientation()
        setContent {
            LauncherTheme {
                SettingsScreen(
                    onBack = { finish() },
                    onOpenRenderSettings = { startActivity(Intent(this, RenderSettingsActivity::class.java)) },
                    onOpenDiagnostics = { startActivity(Intent(this, DiagnosticsActivity::class.java)) },
                    onLaunchOriginal = {
                        val intent = Intent(this, MainActivity::class.java)
                        intent.flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
                        val root = LauncherPrefs.getGameRoot(this)
                        intent.putExtra(MainActivity.EXTRA_GAME_DIR, root)
                        LauncherPrefs.getCustomLaunchFile(this, root)
                            ?.takeIf { it.isNotBlank() }
                            ?.let { path ->
                                val f = File(path)
                                if (f.isFile && f.canRead()) intent.putExtra(MainActivity.EXTRA_LAUNCH_FILE, f.absolutePath)
                            }
                        startActivity(intent)
                        finish()
                    },
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        applyLauncherOrientation()
    }

    private fun applyLauncherOrientation() {
        requestedOrientation = if (ForceLandscapeHelper.isTabletClass(this)) {
            ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        } else {
            ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        }
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
    var useFfmpegImageDecoder by remember { mutableStateOf(LauncherPrefs.getUseFfmpegImageDecoder(context)) }
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
            val games = withContext(Dispatchers.IO) { GameScanner.scan(File(normalized), maxDepth = scanDepth) }
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
    ) { padding ->
        if (landscape) {
            Row(Modifier.fillMaxSize().padding(padding)) {
                SettingsSideBar(dest, { dest = it }, text)
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
                            val games = withContext(Dispatchers.IO) { GameScanner.scan(File(LauncherPrefs.getGameRoot(context)), maxDepth = scanDepth) }
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
                    useFfmpegImageDecoder = useFfmpegImageDecoder,
                    onUseFfmpegImageDecoderChange = { enabled ->
                        useFfmpegImageDecoder = enabled
                        LauncherPrefs.setUseFfmpegImageDecoder(context, enabled)
                    },
                    onOpenDiagnostics = onOpenDiagnostics,
                    onLaunchOriginal = onLaunchOriginal,
                    onExportBackup = {
                        val out = LauncherPrefs.exportBackup(context)
                        statusLine = "${text.exported}: ${out.absolutePath}"
                        scope.launch { snackbarHostState.showSnackbar("${text.exported}: ${out.name}") }
                    },
                    onCopy = { value -> copyToClipboard(context, value); scope.launch { snackbarHostState.showSnackbar(text.aboutCopiedUrl) } },
                    modifier = Modifier.weight(1f).fillMaxHeight().padding(pad),
                )
            }
        } else {
            Column(Modifier.fillMaxSize().padding(padding).padding(pad), verticalArrangement = Arrangement.spacedBy(gap)) {
                SettingsSelectorBar(dest, { dest = it }, text)
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
                    useFfmpegImageDecoder = useFfmpegImageDecoder,
                    onUseFfmpegImageDecoderChange = { enabled ->
                        useFfmpegImageDecoder = enabled
                        LauncherPrefs.setUseFfmpegImageDecoder(context, enabled)
                    },
                    onOpenDiagnostics = onOpenDiagnostics,
                    onLaunchOriginal = onLaunchOriginal,
                    onExportBackup = {
                        val out = LauncherPrefs.exportBackup(context)
                        statusLine = "${text.exported}: ${out.absolutePath}"
                    },
                    onCopy = { value -> copyToClipboard(context, value) },
                    modifier = Modifier.fillMaxWidth().weight(1f),
                )
            }
        }
    }
}

@Composable
private fun SettingsSideBar(dest: SettingsDest, onSelect: (SettingsDest) -> Unit, text: LauncherStrings.Texts) {
    Column(
        Modifier.width(88.dp).fillMaxHeight().padding(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        SettingsSideChip(dest, SettingsDest.Library, onSelect, Icons.Default.FolderOpen, text.settingsLibrary)
        SettingsSideChip(dest, SettingsDest.Display, onSelect, Icons.Default.Language, text.settingsDisplay)
        SettingsSideChip(dest, SettingsDest.Engine, onSelect, Icons.Default.Tune, text.settingsEngine)
        SettingsSideChip(dest, SettingsDest.Tools, onSelect, Icons.Default.BugReport, text.settingsTools)
        SettingsSideChip(dest, SettingsDest.About, onSelect, Icons.Default.Info, text.settingsAbout)
    }
}

@Composable
private fun SettingsSideChip(current: SettingsDest, item: SettingsDest, onSelect: (SettingsDest) -> Unit, icon: ImageVector, label: String) {
    Surface(
        modifier = Modifier.fillMaxWidth().clickable { onSelect(item) },
        color = if (current == item) MaterialTheme.colorScheme.secondaryContainer else MaterialTheme.colorScheme.surfaceContainerLow,
        shape = RoundedCornerShape(18.dp),
    ) {
        Column(Modifier.fillMaxWidth().padding(8.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Icon(icon, null, tint = if (current == item) MaterialTheme.colorScheme.onSecondaryContainer else MaterialTheme.colorScheme.onSurfaceVariant)
            Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis, style = MaterialTheme.typography.labelSmall)
        }
    }
}

@Composable
private fun SettingsSelectorBar(dest: SettingsDest, onSelect: (SettingsDest) -> Unit, text: LauncherStrings.Texts) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        SettingsSelectorChip(dest, SettingsDest.Library, onSelect, Icons.Default.FolderOpen, text.settingsLibrary)
        SettingsSelectorChip(dest, SettingsDest.Display, onSelect, Icons.Default.Language, text.settingsDisplay)
        SettingsSelectorChip(dest, SettingsDest.Engine, onSelect, Icons.Default.Tune, text.settingsEngine)
        SettingsSelectorChip(dest, SettingsDest.Tools, onSelect, Icons.Default.BugReport, text.settingsTools)
        SettingsSelectorChip(dest, SettingsDest.About, onSelect, Icons.Default.Info, text.settingsAbout)
    }
}

@Composable
private fun SettingsSelectorChip(current: SettingsDest, item: SettingsDest, onSelect: (SettingsDest) -> Unit, icon: ImageVector, label: String) {
    val selected = current == item
    Surface(
        modifier = Modifier.clickable { onSelect(item) },
        color = if (selected) MaterialTheme.colorScheme.secondaryContainer else MaterialTheme.colorScheme.surfaceContainerLow,
        shape = RoundedCornerShape(16.dp),
    ) {
        Row(
            Modifier.padding(horizontal = 12.dp, vertical = 9.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Icon(icon, null, tint = if (selected) MaterialTheme.colorScheme.onSecondaryContainer else MaterialTheme.colorScheme.onSurfaceVariant)
            Text(
                label,
                maxLines = 1,
                color = if (selected) MaterialTheme.colorScheme.onSecondaryContainer else MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.labelLarge,
            )
        }
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
    useFfmpegImageDecoder: Boolean,
    onUseFfmpegImageDecoderChange: (Boolean) -> Unit,
    onOpenDiagnostics: () -> Unit,
    onLaunchOriginal: () -> Unit,
    onExportBackup: () -> Unit,
    onCopy: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(modifier.verticalScroll(rememberScrollState())) {
        when (dest) {
            SettingsDest.Library -> LibrarySettings(text, compact, pathInput, onPathChange, scanDepth, onScanDepthChange, statusLine, onSaveAndScan, onRefresh, onGrantStorage)
            SettingsDest.Display -> DisplaySettings(text, compact, onLangChange)
            SettingsDest.Engine -> EngineSettings(text, compact, onOpenRenderSettings, useFfmpegImageDecoder, onUseFfmpegImageDecoderChange)
            SettingsDest.Tools -> ToolsSettings(text, compact, onOpenDiagnostics, onLaunchOriginal, onExportBackup)
            SettingsDest.About -> AboutSettings(text, compact, onCopy)
        }
    }
}

@Composable
private fun SettingsPanel(title: String, icon: ImageVector, compact: Boolean, content: @Composable ColumnScope.() -> Unit) {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow), shape = RoundedCornerShape(20.dp)) {
        Column(Modifier.fillMaxWidth().padding(if (compact) 12.dp else 16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Icon(icon, null, tint = MaterialTheme.colorScheme.primary)
                Text(title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
            }
            content()
        }
    }
}

@Composable
private fun RowSetting(icon: ImageVector, title: String, subtitle: String, onClick: (() -> Unit)? = null, trailing: (@Composable () -> Unit)? = null, showChevron: Boolean = onClick != null) {
    Row(
        Modifier.fillMaxWidth().then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(Modifier.weight(1f), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Icon(icon, null, tint = MaterialTheme.colorScheme.primary)
            Column(Modifier.weight(1f)) {
                Text(title, fontWeight = FontWeight.SemiBold)
                Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2, overflow = TextOverflow.Ellipsis)
            }
        }
        trailing?.invoke()
        if (showChevron) Icon(Icons.AutoMirrored.Filled.OpenInNew, null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun PrimaryChip(text: String, icon: ImageVector, onClick: () -> Unit) {
    ElevatedAssistChip(onClick = onClick, label = { Text(text) }, leadingIcon = { Icon(icon, null) })
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
private fun EngineSettings(
    text: LauncherStrings.Texts,
    compact: Boolean,
    onOpenRenderSettings: () -> Unit,
    useFfmpegImageDecoder: Boolean,
    onUseFfmpegImageDecoderChange: (Boolean) -> Unit,
) {
    SettingsPanel(text.settingsEngine, Icons.Default.Tune, compact) {
        RowSetting(Icons.Default.Tune, text.renderSettings, text.renderSettingsHint, onClick = onOpenRenderSettings)
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        RowSetting(
            Icons.Default.Settings,
            text.ffmpegImageDecoder,
            text.ffmpegImageDecoderHint,
            onClick = { onUseFfmpegImageDecoderChange(!useFfmpegImageDecoder) },
            trailing = {
                Switch(
                    checked = useFfmpegImageDecoder,
                    onCheckedChange = onUseFfmpegImageDecoderChange,
                )
            },
            showChevron = false,
        )
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
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) it.longVersionCode else @Suppress("DEPRECATION") it.versionCode.toLong()
    } ?: 0L
    val scope = rememberCoroutineScope()

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
        RowSetting(Icons.Default.Update, text.aboutCheckUpdate, "Check whether a newer release exists.", onClick = {
            scope.launch { checkLatestRelease() }
        })
    }
}

private sealed interface UpdateResult {
    data object UpToDate : UpdateResult
    data class NewVersion(val tag: String) : UpdateResult
    data object Failed : UpdateResult
}

private suspend fun checkLatestRelease(): UpdateResult = withContext(Dispatchers.IO) {
    runCatching {
        val conn = URL("https://api.github.com/repos/xiaocongyu66/krkr2/releases/latest").openConnection() as HttpURLConnection
        conn.requestMethod = "GET"
        conn.connectTimeout = 8000
        conn.readTimeout = 8000
        conn.setRequestProperty("Accept", "application/vnd.github+json")
        conn.setRequestProperty("User-Agent", "krkr2-launcher")
        conn.inputStream.bufferedReader().use { reader ->
            val json = JSONObject(reader.readText())
            val tag = json.optString("tag_name", "").trim()
            if (tag.isNotBlank()) UpdateResult.NewVersion(tag) else UpdateResult.Failed
        }
    }.getOrElse { UpdateResult.Failed }
}

private fun copyToClipboard(context: Context, text: String) {
    val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
    cm?.setPrimaryClip(ClipData.newPlainText("krkr2", text))
}
