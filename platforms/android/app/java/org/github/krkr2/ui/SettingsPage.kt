package org.github.krkr2.ui

import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import org.github.krkr2.LauncherPrefs
import org.github.krkr2.LauncherStrings
import org.github.krkr2.MainActivity
import org.github.krkr2.ui.components.SectionHeader

/**
 * Settings page — ported from KrKr2-Next's SettingsPage with SectionHeader groups.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsPage(
    text: LauncherStrings.Texts,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    var root by remember { mutableStateOf(LauncherPrefs.getGameRoot(context)) }
    var logPath by remember { mutableStateOf(LauncherPrefs.getLogDir(context)) }
    var forceLandscape by remember { mutableStateOf(LauncherPrefs.getForceLandscape(context)) }
    var lang by remember { mutableStateOf(LauncherPrefs.getLanguage(context)) }
    var status by remember { mutableStateOf("") }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(text.settings) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null)
                    }
                },
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(padding)
                .padding(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            // === Appearance ===
            SectionHeader(icon = Icons.Default.Palette, label = text.sectionAppearance)

            Text(text.language, style = MaterialTheme.typography.titleSmall)
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                FilledTonalButton(onClick = {
                    LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_EN); lang = LauncherPrefs.LANG_EN
                }) { Text(text.english) }
                FilledTonalButton(onClick = {
                    LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_ZH); lang = LauncherPrefs.LANG_ZH
                }) { Text(text.chinese) }
            }

            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Text(text.forceLandscape)
                Switch(checked = forceLandscape, onCheckedChange = {
                    forceLandscape = it; LauncherPrefs.setForceLandscape(context, it)
                })
            }

            // === Library ===
            SectionHeader(icon = Icons.Default.FolderOpen, label = text.sectionLibrary)

            OutlinedTextField(
                value = root, onValueChange = { root = it },
                label = { Text(text.gameRootPath) },
                modifier = Modifier.fillMaxWidth(), singleLine = true,
            )
            OutlinedTextField(
                value = logPath, onValueChange = { logPath = it },
                label = { Text(text.logPath) },
                modifier = Modifier.fillMaxWidth(), singleLine = true,
            )

            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                FilledTonalButton(onClick = {
                    requestStoragePermission(context)
                }) { Text(text.grantStorage) }
                FilledTonalButton(onClick = {
                    LauncherPrefs.setGameRoot(context, root)
                    LauncherPrefs.setLogDir(context, logPath)
                    status = text.scanDone
                }) { Text(text.saveAndScan) }
            }

            // === Debug ===
            SectionHeader(icon = Icons.Default.BugReport, label = text.sectionDebug)

            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                FilledTonalButton(onClick = {
                    LauncherPrefs.setLogDir(context, logPath)
                    runCatching { LauncherPrefs.exportBackup(context) }
                        .onSuccess { file -> status = "${text.exported}: ${file.absolutePath}" }
                        .onFailure { e -> status = e.message.orEmpty() }
                }) { Text(text.exportBackup) }
                FilledTonalButton(onClick = {
                    LauncherPrefs.setLogDir(context, logPath)
                    runCatching { LauncherPrefs.exportLogs(context) }
                        .onSuccess { dir -> status = "${text.logsExported}: ${dir.absolutePath}" }
                        .onFailure { e -> status = e.message.orEmpty() }
                }) { Text(text.exportLogs) }
            }

            FilledTonalButton(onClick = {
                LauncherPrefs.writeLauncherLog(context, "Launch original KRKR2 (from settings)")
                runCatching {
                    val intent = Intent(context, MainActivity::class.java)
                    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
                    context.startActivity(intent)
                }.onFailure { e ->
                    LauncherPrefs.writeLauncherLog(context, "Failed to launch original KRKR2", e)
                    status = e.message.orEmpty()
                }
            }) { Text(text.launchOriginal) }

            // === About ===
            SectionHeader(icon = Icons.Default.Info, label = text.sectionAbout)
            Text(text.aboutVersion, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(text.aboutDescription, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)

            if (status.isNotBlank()) {
                Spacer(Modifier.height(8.dp))
                Text(status, style = MaterialTheme.typography.bodyMedium)
            }

            Spacer(Modifier.height(32.dp))
        }
    }
}

private fun requestStoragePermission(context: android.content.Context) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
        val intent = Intent(
            Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
            Uri.fromParts("package", context.packageName, null),
        )
        runCatching { context.startActivity(intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
            .onFailure { context.startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
    }
}