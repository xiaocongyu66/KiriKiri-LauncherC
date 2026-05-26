package org.github.krkr2

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Language
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Update
import androidx.compose.material.icons.filled.Upload
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeTopAppBar
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
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
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
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.graphics.drawable.toBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

class LauncherSettingsActivity : AppCompatActivity() {
    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        setContent {
            LauncherTheme {
                val context = LocalContext.current
                val activity = this@LauncherSettingsActivity
                var lang by remember { mutableStateOf(LauncherPrefs.getLanguage(context)) }
                val text = if (lang == LauncherPrefs.LANG_ZH) LauncherStrings.zh else LauncherStrings.en
                val scope = rememberCoroutineScope()
                val snackbarHostState = remember { SnackbarHostState() }
                val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())

                var pathInput by remember { mutableStateOf(LauncherPrefs.getGameRoot(context)) }
                var statusLine by remember { mutableStateOf("") }
                var scanDepth by remember { mutableStateOf(LauncherPrefs.getScanDepth(context)) }
                val writeSettingsGranted = Settings.System.canWrite(context)

                fun saveAndScan() {
                    val normalized = pathInput.trim().ifBlank { LauncherPrefs.DEFAULT_GAME_ROOT }
                    LauncherPrefs.setGameRoot(context, normalized)
                    scope.launch {
                        val games = withContext(Dispatchers.IO) {
                            GameScanner.scan(java.io.File(normalized), maxDepth = scanDepth)
                        }
                        statusLine = "${text.start}: ${games.size}"
                        snackbarHostState.showSnackbar("${text.start}: ${games.size}")
                    }
                }

                Scaffold(
                    modifier = Modifier.fillMaxSize().nestedScroll(scrollBehavior.nestedScrollConnection),
                    snackbarHost = { SnackbarHost(snackbarHostState) },
                    topBar = {
                        LargeTopAppBar(
                            title = { Text(text.settings) },
                            navigationIcon = {
                                IconButton(onClick = { finish() }) {
                                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = text.close)
                                }
                            },
                            scrollBehavior = scrollBehavior,
                            colors = TopAppBarDefaults.largeTopAppBarColors(
                                containerColor = MaterialTheme.colorScheme.background,
                                scrolledContainerColor = MaterialTheme.colorScheme.surface,
                            ),
                        )
                    },
                ) { padding ->
                    Box(Modifier.fillMaxSize().padding(padding)) {
                        Column(
                            modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
                            verticalArrangement = Arrangement.spacedBy(16.dp),
                        ) {
                            SettingsHero(
                                title = text.settings,
                                subtitle = text.settingsAbout,
                                actionText = text.saveAndScan,
                                onAction = { saveAndScan() },
                                secondaryText = text.refresh,
                                onSecondaryAction = {
                                    scope.launch {
                                        val games = withContext(Dispatchers.IO) {
                                            GameScanner.scan(java.io.File(LauncherPrefs.getGameRoot(context)), maxDepth = scanDepth)
                                        }
                                        statusLine = "${text.start}: ${games.size}"
                                        snackbarHostState.showSnackbar("${text.start}: ${games.size}")
                                    }
                                },
                                icon = Icons.Default.Tune,
                                statusLine = statusLine,
                            )

                            Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                                SettingsSection(text.settingsLibrary, LauncherTokens.EngineAccent) {
                                    SettingRow(icon = Icons.Default.FolderOpen, title = text.gameRootPath, subtitle = "Set the folder that contains your games.") {
                                        OutlinedTextField(
                                            value = pathInput,
                                            onValueChange = { pathInput = it },
                                            label = { Text(text.gameRootPath) },
                                            singleLine = true,
                                            modifier = Modifier.fillMaxWidth(),
                                        )
                                        Spacer(Modifier.height(12.dp))
                                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                            PrimaryChip(text = text.saveAndScan, icon = Icons.Default.Save, onClick = { saveAndScan() })
                                            PrimaryChip(text = text.refresh, icon = Icons.Default.Refresh, onClick = {
                                                scope.launch {
                                                    val games = withContext(Dispatchers.IO) {
                                                        GameScanner.scan(java.io.File(LauncherPrefs.getGameRoot(context)), maxDepth = scanDepth)
                                                    }
                                                    statusLine = "${text.start}: ${games.size}"
                                                    snackbarHostState.showSnackbar("${text.start}: ${games.size}")
                                                }
                                            })
                                        }
                                        if (statusLine.isNotEmpty()) {
                                            Spacer(Modifier.height(8.dp))
                                            Text(statusLine, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
                                        }
                                    }
                                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                                    RowSetting(
                                        icon = Icons.Default.FolderOpen,
                                        title = text.grantStorage,
                                        subtitle = "Allow access to your game folder.",
                                        onClick = {
                                            val intent = Intent(
                                                Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                                Uri.fromParts("package", context.packageName, null),
                                            )
                                            try {
                                                startActivity(intent)
                                            } catch (e: ActivityNotFoundException) {
                                                scope.launch { snackbarHostState.showSnackbar(e.message ?: "") }
                                            }
                                        },
                                    )
                                }

                                SettingsSection(text.settingsDisplay, Color.Unspecified) {
                                    RowSetting(
                                        icon = Icons.Default.Language,
                                        title = text.language,
                                        subtitle = text.languageHint,
                                        trailing = {
                                            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                                TextButton(onClick = { LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_EN); lang = LauncherPrefs.LANG_EN }) { Text("EN") }
                                                TextButton(onClick = { LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_ZH); lang = LauncherPrefs.LANG_ZH }) { Text("中") }
                                            }
                                        }
                                    )
                                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                                    Column(Modifier.fillMaxWidth().padding(16.dp)) {
                                        Text(text.scanDepth, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                                        Text("1–10. Deeper scans find more nested games but take longer.", color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
                                        Spacer(Modifier.height(8.dp))
                                        Slider(
                                            value = scanDepth.toFloat(),
                                            onValueChange = { scanDepth = it.toInt().coerceIn(1, 10) },
                                            valueRange = 1f..10f,
                                            steps = 8,
                                            onValueChangeFinished = {
                                                LauncherPrefs.setScanDepth(context, scanDepth)
                                            },
                                        )
                                        Text("${scanDepth}/10", color = MaterialTheme.colorScheme.onSurfaceVariant)
                                    }
                                }

                                SettingsSection(text.settingsEngine, LauncherTokens.EngineAccent) {
                                    RowSetting(
                                        icon = Icons.Default.Tune,
                                        title = text.renderSettings,
                                        subtitle = "Open engine rendering options.",
                                        onClick = { startActivity(Intent(activity, RenderSettingsActivity::class.java)) },
                                    )
                                }

                                SettingsSection(text.settingsTools, Color.Unspecified) {
                                    RowSetting(icon = Icons.Default.BugReport, title = text.diagnostics, subtitle = "Inspect launcher and engine logs.", onClick = { startActivity(Intent(activity, DiagnosticsActivity::class.java)) })
                                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                                    RowSetting(icon = Icons.AutoMirrored.Filled.OpenInNew, title = text.launchOriginal, subtitle = "Open the original KRKR launcher.", onClick = {
                                        val intent = Intent(activity, MainActivity::class.java)
                                        intent.flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
                                        startActivity(intent)
                                        activity.finish()
                                    })
                                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                                    RowSetting(icon = Icons.Default.Upload, title = text.exportBackup, subtitle = "Export launcher preferences to a backup file.", onClick = {
                                        val out = LauncherPrefs.exportBackup(context)
                                        statusLine = "${text.exported}: ${out.absolutePath}"
                                        scope.launch { snackbarHostState.showSnackbar("${text.exported}: ${out.name}") }
                                    })
                                }

                                SettingsSection(text.settingsAbout, Color.Unspecified) {
                                    val pInfo = remember { runCatching { packageManager.getPackageInfo(packageName, 0) }.getOrNull() }
                                    val appIcon = remember { runCatching { packageManager.getApplicationIcon(packageName) }.getOrNull() }
                                    val versionName = pInfo?.versionName ?: "—"
                                    val versionCode = pInfo?.let {
                                        @Suppress("DEPRECATION")
                                        if (android.os.Build.VERSION.SDK_INT >= 28) it.longVersionCode else it.versionCode.toLong()
                                    } ?: 0L
                                    var checking by remember { mutableStateOf(false) }

                                    Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                                            Surface(color = MaterialTheme.colorScheme.primaryContainer, shape = RoundedCornerShape(20.dp)) {
                                                Box(Modifier.size(56.dp), contentAlignment = Alignment.Center) {
                                                    if (appIcon != null) {
                                                        Image(bitmap = appIcon.toBitmap(56, 56).asImageBitmap(), contentDescription = null, modifier = Modifier.size(56.dp))
                                                    } else {
                                                        Icon(Icons.Default.Info, null, tint = MaterialTheme.colorScheme.onPrimaryContainer)
                                                    }
                                                }
                                            }
                                            Column(Modifier.weight(1f)) {
                                                Text(text.aboutTitle, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                                                Text(text.aboutOpenSourceUrl, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
                                            }
                                        }
                                        RowSetting(icon = Icons.Default.Info, title = text.appVersion, subtitle = "$versionName (#$versionCode)", onClick = {}, showChevron = false)
                                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                                        RowSetting(icon = Icons.Default.Code, title = text.aboutOpenSource, subtitle = text.aboutOpenSourceUrl, onClick = {
                                            val intent = Intent(Intent.ACTION_VIEW, Uri.parse(text.aboutOpenSourceUrl))
                                            try {
                                                startActivity(intent)
                                            } catch (_: ActivityNotFoundException) {
                                                copyToClipboard(context, text.aboutOpenSourceUrl)
                                                scope.launch { snackbarHostState.showSnackbar(text.aboutCopiedUrl) }
                                            }
                                        })
                                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                                        RowSetting(icon = Icons.Default.Update, title = if (checking) text.aboutCheckingUpdate else text.aboutCheckUpdate, subtitle = "Check whether a newer release exists.", onClick = {
                                            if (checking) return@RowSetting
                                            checking = true
                                            scope.launch {
                                                val result = checkUpdate(versionName)
                                                checking = false
                                                snackbarHostState.showSnackbar(
                                                    when (result) {
                                                        is UpdateResult.UpToDate -> text.aboutAlreadyLatest
                                                        is UpdateResult.NewVersion -> "${text.aboutNewVersion} ${result.tag}"
                                                        is UpdateResult.Failed -> text.aboutUpdateFailed
                                                    }
                                                )
                                            }
                                        })
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun SettingsHero(
    title: String,
    subtitle: String,
    actionText: String,
    onAction: () -> Unit,
    secondaryText: String,
    onSecondaryAction: () -> Unit,
    icon: ImageVector,
    statusLine: String,
) {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHighest), shape = RoundedCornerShape(24.dp)) {
        Column(Modifier.fillMaxWidth().padding(20.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Surface(color = MaterialTheme.colorScheme.primaryContainer, shape = RoundedCornerShape(16.dp)) {
                    Box(Modifier.size(48.dp), contentAlignment = Alignment.Center) { Icon(icon, null, tint = MaterialTheme.colorScheme.onPrimaryContainer) }
                }
                Column(Modifier.weight(1f)) {
                    Text(title, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.SemiBold)
                    Text(subtitle, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                PrimaryChip(text = actionText, icon = Icons.Default.Save, onClick = onAction)
                PrimaryChip(text = secondaryText, icon = Icons.Default.Refresh, onClick = onSecondaryAction)
            }
            if (statusLine.isNotBlank()) {
                Text(statusLine, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
private fun SettingsSection(title: String, accent: Color, content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth()) {
        Text(
            text = title.uppercase(),
            style = MaterialTheme.typography.labelMedium,
            color = if (accent == Color.Unspecified) MaterialTheme.colorScheme.primary else accent,
            modifier = Modifier.padding(start = 4.dp, bottom = 8.dp),
        )
        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface), shape = RoundedCornerShape(24.dp)) {
            Column(Modifier.fillMaxWidth()) { content() }
        }
    }
}

@Composable
private fun SettingRow(
    icon: ImageVector,
    title: String,
    subtitle: String? = null,
    content: @Composable () -> Unit,
) {
    Column(Modifier.fillMaxWidth()) {
        RowSetting(icon = icon, title = title, subtitle = subtitle, showChevron = false)
        Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
            content()
        }
    }
}

@Composable
private fun RowSetting(
    icon: ImageVector,
    title: String,
    subtitle: String? = null,
    accent: Color? = null,
    onClick: (() -> Unit)? = null,
    showChevron: Boolean = onClick != null,
    trailing: (@Composable () -> Unit)? = null,
) {
    Surface(onClick = { onClick?.invoke() }, enabled = onClick != null, color = Color.Transparent, modifier = Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Surface(color = (accent ?: MaterialTheme.colorScheme.secondaryContainer), shape = RoundedCornerShape(14.dp)) {
                Box(Modifier.size(40.dp), contentAlignment = Alignment.Center) {
                    Icon(icon, null, tint = if (accent == null) MaterialTheme.colorScheme.onSecondaryContainer else accent)
                }
            }
            Spacer(Modifier.size(12.dp))
            Column(Modifier.weight(1f)) {
                Text(title, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium)
                if (subtitle != null) {
                    Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            if (trailing != null) trailing() else if (showChevron) Icon(Icons.Filled.ChevronRight, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun PrimaryChip(text: String, icon: ImageVector, onClick: () -> Unit) {
    androidx.compose.material3.ElevatedAssistChip(onClick = onClick, label = { Text(text) }, leadingIcon = { Icon(icon, null, modifier = Modifier.size(16.dp)) })
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
