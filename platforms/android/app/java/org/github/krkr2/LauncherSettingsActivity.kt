package org.github.krkr2

import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

class LauncherSettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            LauncherTheme {
                val context = this
                var root by remember { mutableStateOf(LauncherPrefs.getGameRoot(context)) }
                var logPath by remember { mutableStateOf(LauncherPrefs.getLogDir(context)) }
                var forceLandscape by remember { mutableStateOf(LauncherPrefs.getForceLandscape(context)) }
                var lang by remember { mutableStateOf(LauncherPrefs.getLanguage(context)) }
                var status by remember { mutableStateOf("") }
                val text = if (lang == LauncherPrefs.LANG_ZH) LauncherStrings.zh else LauncherStrings.en

                Surface(Modifier.fillMaxSize()) {
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .verticalScroll(rememberScrollState())
                            .padding(20.dp),
                        verticalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Text(text.settings, style = MaterialTheme.typography.headlineSmall)

                        OutlinedTextField(
                            value = root,
                            onValueChange = { root = it },
                            label = { Text(text.gameRootPath) },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true
                        )

                        OutlinedTextField(
                            value = logPath,
                            onValueChange = { logPath = it },
                            label = { Text(text.logPath) },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true
                        )

                        Text(text.language, style = MaterialTheme.typography.titleMedium)
                        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            FilledTonalButton(onClick = {
                                LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_EN)
                                lang = LauncherPrefs.LANG_EN
                            }) { Text(text.english) }
                            FilledTonalButton(onClick = {
                                LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_ZH)
                                lang = LauncherPrefs.LANG_ZH
                            }) { Text(text.chinese) }
                        }

                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            Text(text.forceLandscape)
                            Switch(checked = forceLandscape, onCheckedChange = {
                                forceLandscape = it
                                LauncherPrefs.setForceLandscape(context, it)
                            })
                        }

                        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            FilledTonalButton(onClick = { requestStoragePermission() }) { Text(text.grantStorage) }
                            FilledTonalButton(onClick = {
                                LauncherPrefs.setGameRoot(context, root)
                                LauncherPrefs.setLogDir(context, logPath)
                                LauncherPrefs.setForceLandscape(context, forceLandscape)
                                status = text.scanDone
                            }) { Text(text.saveAndScan) }
                        }

                        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            FilledTonalButton(onClick = {
                                LauncherPrefs.setLogDir(context, logPath)
                                runCatching { LauncherPrefs.exportBackup(context) }
                                    .onSuccess { file -> status = "${text.exported}: ${file.absolutePath}" }
                                    .onFailure { error -> status = error.message.orEmpty() }
                            }) { Text(text.exportBackup) }
                            FilledTonalButton(onClick = {
                                LauncherPrefs.setLogDir(context, logPath)
                                runCatching { LauncherPrefs.exportLogs(context) }
                                    .onSuccess { dir -> status = "${text.logsExported}: ${dir.absolutePath}" }
                                    .onFailure { error -> status = error.message.orEmpty() }
                            }) { Text(text.exportLogs) }
                        }

                        Button(onClick = {
                            LauncherPrefs.setGameRoot(context, root)
                            LauncherPrefs.setLogDir(context, logPath)
                            LauncherPrefs.setForceLandscape(context, forceLandscape)
                            finish()
                        }) {
                            Text(text.save)
                        }

                        FilledTonalButton(onClick = {
                            LauncherPrefs.writeLauncherLog(context, "Launch original KRKR2 (from settings)")
                            runCatching {
                                val intent = Intent(context, MainActivity::class.java)
                                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
                                startActivity(intent)
                            }.onFailure { error ->
                                LauncherPrefs.writeLauncherLog(context, "Failed to launch original KRKR2", error)
                                status = error.message.orEmpty()
                            }
                        }) { Text(text.launchOriginal) }

                        if (status.isNotBlank()) {
                            Text(status, style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                }
            }
        }
    }

    private fun requestStoragePermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            if (ContextCompat.checkSelfPermission(this, android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED
            ) {
                ActivityCompat.requestPermissions(
                    this,
                    arrayOf(android.Manifest.permission.READ_EXTERNAL_STORAGE, android.Manifest.permission.WRITE_EXTERNAL_STORAGE),
                    REQUEST_STORAGE_PERMISSION
                )
            }
            return
        }

        val perAppIntent = Intent(
            Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
            Uri.fromParts("package", packageName, null)
        )
        runCatching { startActivity(perAppIntent) }
            .onFailure { startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)) }
    }

    private companion object {
        const val REQUEST_STORAGE_PERMISSION = 2026
    }

}
