package org.github.krkr2

import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
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
import androidx.compose.material.icons.automirrored.filled.RotateRight
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Language
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Upload
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Launcher settings, organised by Material 3 grouped lists.
 *
 * Five top-level groups, each rendered as a card with a heading and a stack
 * of "rows". A row is either a switch (toggle) or a navigation row (chevron
 * → drills down to another activity / external action). This mirrors how
 * iOS / Pixel Settings app organises its content and gives the engine
 * config (krkr renderer prefs) a first-class slot without burying it under
 * launcher chrome.
 *
 * Groups:
 *  - Library — game scan path, rescan, custom image dirs
 *  - Display — language, force landscape, write_settings grant
 *  - Engine (krkr) — opens RenderSettingsActivity, per-game overrides
 *  - Tools — diagnostics, launch original, export backup
 *  - About — version info
 */
class LauncherSettingsActivity : AppCompatActivity() {

    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            LauncherTheme {
                val context = LocalContext.current
                val activity = this@LauncherSettingsActivity
                var lang by remember { mutableStateOf(LauncherPrefs.getLanguage(context)) }
                val text = if (lang == LauncherPrefs.LANG_ZH) LauncherStrings.zh else LauncherStrings.en
                val scope = rememberCoroutineScope()
                val snackbarHostState = remember { SnackbarHostState() }
                val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(
                    rememberTopAppBarState()
                )

                // ---- Library state ----
                var pathInput by remember {
                    mutableStateOf(LauncherPrefs.getGameRoot(context))
                }
                var statusLine by remember { mutableStateOf("") }

                fun saveAndScan() {
                    val normalized = pathInput.trim().ifBlank { LauncherPrefs.DEFAULT_GAME_ROOT }
                    LauncherPrefs.setGameRoot(context, normalized)
                    scope.launch {
                        val games = withContext(Dispatchers.IO) {
                            GameScanner.scan(java.io.File(normalized))
                        }
                        statusLine = "${text.start}: ${games.size}"
                        snackbarHostState.showSnackbar("${text.start}: ${games.size}")
                    }
                }

                // ---- Display state ----
                var forceLandscape by remember {
                    mutableStateOf(LauncherPrefs.getForceLandscape(context))
                }
                val writeSettingsGranted =
                    Settings.System.canWrite(context)

                Scaffold(
                    modifier = Modifier
                        .fillMaxSize()
                        .nestedScroll(scrollBehavior.nestedScrollConnection),
                    snackbarHost = { SnackbarHost(snackbarHostState) },
                    topBar = {
                        LargeTopAppBar(
                            title = { Text(text.settings) },
                            navigationIcon = {
                                IconButton(onClick = { finish() }) {
                                    Icon(
                                        Icons.AutoMirrored.Filled.ArrowBack,
                                        contentDescription = text.close,
                                    )
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
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(padding)
                            .verticalScroll(rememberScrollState())
                            .padding(horizontal = 16.dp),
                        verticalArrangement = Arrangement.spacedBy(16.dp),
                    ) {
                        Spacer(Modifier.height(4.dp))

                        // ---- Library ----
                        SettingsGroup(title = text.settingsLibrary) {
                            Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
                                OutlinedTextField(
                                    value = pathInput,
                                    onValueChange = { pathInput = it },
                                    label = { Text(text.gameRootPath) },
                                    singleLine = true,
                                    modifier = Modifier.fillMaxWidth(),
                                )
                                Spacer(Modifier.height(8.dp))
                                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    AssistChip(
                                        onClick = { saveAndScan() },
                                        label = { Text(text.saveAndScan) },
                                        leadingIcon = {
                                            Icon(Icons.Default.Save, null, modifier = Modifier.size(16.dp))
                                        },
                                    )
                                    AssistChip(
                                        onClick = {
                                            scope.launch {
                                                val games = withContext(Dispatchers.IO) {
                                                    GameScanner.scan(java.io.File(LauncherPrefs.getGameRoot(context)))
                                                }
                                                statusLine = "${text.start}: ${games.size}"
                                                snackbarHostState.showSnackbar(
                                                    "${text.start}: ${games.size}"
                                                )
                                            }
                                        },
                                        label = { Text(text.refresh) },
                                        leadingIcon = {
                                            Icon(Icons.Default.Refresh, null, modifier = Modifier.size(16.dp))
                                        },
                                    )
                                }
                                if (statusLine.isNotEmpty()) {
                                    Spacer(Modifier.height(8.dp))
                                    Text(
                                        statusLine,
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    )
                                }
                            }
                        }

                        // ---- Display ----
                        SettingsGroup(title = text.settingsDisplay) {
                            NavRow(
                                icon = Icons.Default.Language,
                                title = text.language,
                                trailing = {
                                    Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                        TextButton(onClick = {
                                            LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_EN)
                                            lang = LauncherPrefs.LANG_EN
                                        }) { Text("EN") }
                                        TextButton(onClick = {
                                            LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_ZH)
                                            lang = LauncherPrefs.LANG_ZH
                                        }) { Text("中") }
                                    }
                                },
                            )
                            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant, thickness = 0.5.dp)
                            ToggleRow(
                                icon = Icons.AutoMirrored.Filled.RotateRight,
                                title = text.forceLandscape,
                                checked = forceLandscape,
                                onCheckedChange = {
                                    forceLandscape = it
                                    LauncherPrefs.setForceLandscape(context, it)
                                },
                            )
                            if (!writeSettingsGranted) {
                                HorizontalDivider(
                                    color = MaterialTheme.colorScheme.outlineVariant,
                                    thickness = 0.5.dp,
                                )
                                NavRow(
                                    icon = Icons.AutoMirrored.Filled.RotateRight,
                                    title = text.grantWriteSettings,
                                    subtitle = text.writeSettingsHint,
                                    onClick = {
                                        try {
                                            val intent = Intent(
                                                Settings.ACTION_MANAGE_WRITE_SETTINGS,
                                                Uri.parse("package:${context.packageName}"),
                                            )
                                            startActivity(intent)
                                        } catch (e: ActivityNotFoundException) {
                                            scope.launch {
                                                snackbarHostState.showSnackbar(e.message ?: "")
                                            }
                                        }
                                    },
                                )
                            }
                        }

                        // ---- Engine (krkr) ----
                        SettingsGroup(
                            title = text.settingsEngine,
                            accent = LauncherTokens.EngineAccent,
                        ) {
                            NavRow(
                                icon = Icons.Default.Tune,
                                title = text.renderSettings,
                                subtitle = text.renderSettingsHint,
                                accent = LauncherTokens.EngineAccent,
                                onClick = {
                                    startActivity(Intent(activity, RenderSettingsActivity::class.java))
                                },
                            )
                        }

                        // ---- Tools ----
                        SettingsGroup(title = text.settingsTools) {
                            NavRow(
                                icon = Icons.Default.BugReport,
                                title = text.diagnostics,
                                onClick = {
                                    startActivity(Intent(activity, DiagnosticsActivity::class.java))
                                },
                            )
                            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant, thickness = 0.5.dp)
                            NavRow(
                                icon = Icons.AutoMirrored.Filled.OpenInNew,
                                title = text.launchOriginal,
                                onClick = {
                                    val intent = Intent(activity, MainActivity::class.java)
                                    intent.flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or
                                        Intent.FLAG_ACTIVITY_SINGLE_TOP
                                    startActivity(intent)
                                    activity.finish()
                                },
                            )
                            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant, thickness = 0.5.dp)
                            NavRow(
                                icon = Icons.Default.Upload,
                                title = text.exportBackup,
                                onClick = {
                                    val out = LauncherPrefs.exportBackup(context)
                                    statusLine = "${text.exported}: ${out.absolutePath}"
                                    scope.launch {
                                        snackbarHostState.showSnackbar("${text.exported}: ${out.name}")
                                    }
                                },
                            )
                        }

                        // ---- About ----
                        SettingsGroup(title = text.settingsAbout) {
                            val pInfo = remember {
                                runCatching {
                                    packageManager.getPackageInfo(packageName, 0)
                                }.getOrNull()
                            }
                            NavRow(
                                icon = Icons.Default.Info,
                                title = text.appVersion,
                                subtitle = pInfo?.versionName ?: "—",
                                onClick = {},
                                showChevron = false,
                            )
                        }

                        Spacer(Modifier.height(24.dp))
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/*  Reusable building blocks                                                  */
/* ------------------------------------------------------------------------- */

@Composable
private fun SettingsGroup(
    title: String,
    accent: Color = MaterialTheme.colorScheme.primary,
    content: @Composable () -> Unit,
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Text(
            text = title.uppercase(),
            style = MaterialTheme.typography.labelMedium,
            color = accent,
            modifier = Modifier.padding(start = 16.dp, bottom = 8.dp, top = 4.dp),
        )
        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surface,
            ),
        ) {
            Column { content() }
        }
    }
}

@Composable
private fun NavRow(
    icon: ImageVector,
    title: String,
    subtitle: String? = null,
    onClick: (() -> Unit)? = null,
    showChevron: Boolean = onClick != null,
    accent: Color? = null,
    trailing: (@Composable () -> Unit)? = null,
) {
    Surface(
        onClick = { onClick?.invoke() },
        enabled = onClick != null,
        color = Color.Transparent,
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier = Modifier.size(40.dp),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    icon,
                    contentDescription = null,
                    tint = accent ?: MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Spacer(Modifier.size(8.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    title,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface,
                )
                if (subtitle != null) {
                    Text(
                        subtitle,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            if (trailing != null) {
                trailing()
            } else if (showChevron) {
                Icon(
                    Icons.Default.ChevronRight,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun ToggleRow(
    icon: ImageVector,
    title: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(modifier = Modifier.size(40.dp), contentAlignment = Alignment.Center) {
            Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Spacer(Modifier.size(8.dp))
        Text(
            title,
            style = MaterialTheme.typography.bodyLarge,
            modifier = Modifier.weight(1f),
        )
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}
