package org.github.krkr2

import android.content.Intent
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
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
import androidx.compose.material3.Button
import androidx.compose.material3.Divider
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

/**
 * Single Settings activity that owns every preference that used to clutter
 * the launcher home screen:
 *   - Game library path (was the OutlinedTextField + Save&Scan + Reload)
 *   - Language switch                              (was inline buttons)
 *   - Force landscape + WRITE_SETTINGS grant       (kept here)
 *   - Launch original KRKR FileSelector            (was on the home row)
 *   - Export launcher backup                        (was on the home row)
 *
 * The home screen now only shows Storage permission, Scan, and the
 * landscape switch. Everything else lives here.
 *
 * Per-game overrides (renderer / language / launch-file / etc.) are not in
 * this activity. They belong on the GameDetail panel as a future step,
 * keyed by gameDir, so each game can shadow these globals.
 */
class LauncherSettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            LauncherTheme {
                val context = this
                var root by remember { mutableStateOf(LauncherPrefs.getGameRoot(context)) }
                var lang by remember { mutableStateOf(LauncherPrefs.getLanguage(context)) }
                var forceLandscape by remember { mutableStateOf(LauncherPrefs.getForceLandscape(context)) }
                var canWriteSettings by remember {
                    mutableStateOf(ForceLandscapeHelper.canWriteSystemSettings(context))
                }
                var statusLine by remember { mutableStateOf<String?>(null) }
                val text = LauncherStrings.current(context)
                Surface(Modifier.fillMaxSize(), color = Color(0xFF0C0C10)) {
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(16.dp)
                            .verticalScroll(rememberScrollState()),
                        verticalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Text(
                            text.settings,
                            style = MaterialTheme.typography.headlineSmall,
                            color = Color.White,
                        )
                        statusLine?.let {
                            Text(it, color = Color(0xFF80FF80), style = MaterialTheme.typography.bodySmall)
                        }

                        // ---- Game library ---------------------------------
                        SettingsCard(title = text.gameRootPath) {
                            OutlinedTextField(
                                value = root,
                                onValueChange = { root = it },
                                label = { Text(text.gameRootPath) },
                                singleLine = true,
                                modifier = Modifier.fillMaxWidth(),
                            )
                            Spacer(Modifier.height(8.dp))
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                                FilledTonalButton(
                                    onClick = {
                                        LauncherPrefs.setGameRoot(context, root)
                                        statusLine = text.save
                                    },
                                    modifier = Modifier.weight(1f),
                                ) { Text(text.save) }
                                OutlinedButton(
                                    onClick = {
                                        root = LauncherPrefs.DEFAULT_GAME_ROOT
                                    },
                                    modifier = Modifier.weight(1f),
                                ) { Text(text.reloadSaved) }
                            }
                        }

                        // ---- Language -------------------------------------
                        SettingsCard(title = text.language) {
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                FilledTonalButton(onClick = {
                                    LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_EN)
                                    lang = LauncherPrefs.LANG_EN
                                }) { Text(text.english) }
                                FilledTonalButton(onClick = {
                                    LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_ZH)
                                    lang = LauncherPrefs.LANG_ZH
                                }) { Text(text.chinese) }
                            }
                        }

                        // ---- Orientation ----------------------------------
                        SettingsCard(title = text.forceLandscape) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(text.forceLandscape, color = Color.White, modifier = Modifier.weight(1f))
                                Switch(checked = forceLandscape, onCheckedChange = {
                                    forceLandscape = it
                                    LauncherPrefs.setForceLandscape(context, it)
                                })
                            }
                            Spacer(Modifier.height(6.dp))
                            FilledTonalButton(
                                onClick = { ForceLandscapeHelper.requestPermission(context) },
                                enabled = !canWriteSettings,
                            ) {
                                Text(if (canWriteSettings) text.writeSettingsGranted else text.grantWriteSettings)
                            }
                            Spacer(Modifier.height(4.dp))
                            Text(
                                text.writeSettingsHint,
                                color = Color(0xFFAAAAAA),
                                style = MaterialTheme.typography.bodySmall,
                            )
                        }

                        // ---- Tools / Data ---------------------------------
                        SettingsCard(title = text.diagnostics) {
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                                FilledTonalButton(
                                    onClick = {
                                        startActivity(Intent(context, DiagnosticsActivity::class.java))
                                    },
                                    modifier = Modifier.weight(1f),
                                ) { Text(text.diagnostics) }
                                FilledTonalButton(
                                    onClick = {
                                        val intent = Intent(context, MainActivity::class.java)
                                        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
                                        startActivity(intent)
                                    },
                                    modifier = Modifier.weight(1f),
                                ) { Text(text.launchOriginal) }
                            }
                            Spacer(Modifier.height(8.dp))
                            FilledTonalButton(
                                onClick = {
                                    val file = LauncherPrefs.exportBackup(context)
                                    statusLine = "${text.exported}: ${file.absolutePath}"
                                },
                                modifier = Modifier.fillMaxWidth(),
                            ) { Text(text.exportBackup) }
                        }

                        Divider(color = Color(0xFF333333))
                        Button(
                            onClick = {
                                // Save root + force-landscape + recheck the
                                // permission state in case the user just
                                // returned from the system page.
                                LauncherPrefs.setGameRoot(context, root)
                                LauncherPrefs.setForceLandscape(context, forceLandscape)
                                canWriteSettings = ForceLandscapeHelper.canWriteSystemSettings(context)
                                finish()
                            },
                            modifier = Modifier.fillMaxWidth(),
                        ) { Text(text.save) }
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        // canWriteSystemSettings re-reads on next recomposition; nothing to
        // do here. Compose state survives onResume so the UI refreshes
        // automatically after the round-trip to the system page.
    }
}

@Composable
private fun SettingsCard(title: String, content: @Composable () -> Unit) {
    ElevatedCard(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium, color = Color.White)
            Spacer(Modifier.height(8.dp))
            content()
        }
    }
}