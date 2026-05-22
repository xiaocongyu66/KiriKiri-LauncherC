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
                        Button(onClick = {
                            LauncherPrefs.setGameRoot(context, root)
                            LauncherPrefs.setForceLandscape(context, forceLandscape)
                            finish()
                        }) {
                            Text(text.save)
                        }
                    }
                }
            }
        }
    }
}