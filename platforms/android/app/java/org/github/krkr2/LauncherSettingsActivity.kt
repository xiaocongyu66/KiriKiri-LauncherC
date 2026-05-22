package org.github.krkr2

import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

class LauncherSettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            LauncherTheme {
                val context = this
                var root by remember { mutableStateOf(LauncherPrefs.getGameRoot(context)) }
                var forceLandscape by remember { mutableStateOf(LauncherPrefs.getForceLandscape(context)) }
                // canWrite is read once per recomposition; the Settings page
                // pops out of process so we need to refresh on resume.
                var canWriteSettings by remember {
                    mutableStateOf(ForceLandscapeHelper.canWriteSystemSettings(context))
                }
                val text = LauncherStrings.current(context)
                Surface(Modifier.fillMaxSize()) {
                    Column(
                        modifier = Modifier.fillMaxSize().padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Text(text.settings)
                        OutlinedTextField(
                            value = root,
                            onValueChange = { root = it },
                            label = { Text(text.gameRootPath) },
                            modifier = Modifier.fillMaxWidth()
                        )
                        Text(text.language)
                        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            FilledTonalButton(onClick = {
                                LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_EN)
                            }) { Text(text.english) }
                            FilledTonalButton(onClick = {
                                LauncherPrefs.setLanguage(context, LauncherPrefs.LANG_ZH)
                            }) { Text(text.chinese) }
                        }
                        Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                            Text(text.forceLandscape)
                            Switch(checked = forceLandscape, onCheckedChange = {
                                forceLandscape = it
                                LauncherPrefs.setForceLandscape(context, it)
                            })
                        }
                        // The actual rotation lock requires WRITE_SETTINGS to
                        // also flip the system auto-rotate flag. Show the
                        // current grant state and a button to open the
                        // system page when it's missing.
                        Row(
                            verticalAlignment = androidx.compose.ui.Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            FilledTonalButton(
                                onClick = {
                                    ForceLandscapeHelper.requestPermission(context)
                                },
                                enabled = !canWriteSettings,
                            ) {
                                Text(if (canWriteSettings) text.writeSettingsGranted else text.grantWriteSettings)
                            }
                        }
                        Text(text.writeSettingsHint)
                        Button(onClick = {
                            LauncherPrefs.setGameRoot(context, root)
                            LauncherPrefs.setForceLandscape(context, forceLandscape)
                            // Re-check after possible system page round-trip.
                            canWriteSettings = ForceLandscapeHelper.canWriteSystemSettings(context)
                            finish()
                        }) {
                            Text(text.save)
                        }
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        // No-op recomposition trigger; if the user just returned from the
        // system "Modify system settings" page the next recomposition will
        // re-read canWriteSystemSettings(). Compose state survives onResume
        // so this is enough to refresh the button label.
    }
}