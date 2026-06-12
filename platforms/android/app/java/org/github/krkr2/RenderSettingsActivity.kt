package org.github.krkr2

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
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
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
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshots.SnapshotStateMap
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch

/**
 * Krkr engine renderer/preferences editor.
 *
 * Reads the engine's `GlobalPreference.xml` straight from the app's filesDir
 * and renders a Compose UI driven by [KrkrPrefsSchema]. Writes go back to
 * the same XML so the engine picks them up on next launch.
 *
 * Why a separate activity: the schema has ~25 toggles spread across three
 * sections; embedding it in LauncherSettingsActivity would dwarf the other
 * launcher prefs. Keeping it standalone also makes the per-game override
 * activity (planned next) trivial — same UI, different storage backend.
 */
class RenderSettingsActivity : AppCompatActivity() {
    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            LauncherTheme {
                val context = LocalContext.current
                val text = LauncherStrings.current(context)
                val scope = rememberCoroutineScope()
                val snackbarHostState = remember { SnackbarHostState() }
                val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(
                    rememberTopAppBarState()
                )

                // Snapshot state, hydrated from XML once on entry.
                val values: SnapshotStateMap<String, String> = remember { mutableStateMapOf() }
                LaunchedEffect(Unit) {
                    val snap = KrkrPrefsStore.load(context)
                    KrkrPrefsSchema.ALL_BY_KEY.values.forEach { item ->
                        val current = snap.items[item.key] ?: item.defaultAsString()
                        values[item.key] =
                            if (item.key == "renderer") LauncherPrefs.normalizeRendererPreference(current) else current
                    }
                }

                Scaffold(
                    modifier = Modifier
                        .fillMaxSize()
                        .nestedScroll(scrollBehavior.nestedScrollConnection),
                    snackbarHost = { SnackbarHost(snackbarHostState) },
                    topBar = {
                        LargeTopAppBar(
                            title = { Text(text.renderSettings) },
                            navigationIcon = {
                                IconButton(onClick = { finish() }) {
                                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = text.close)
                                }
                            },
                            actions = {
                                IconButton(onClick = {
                                    KrkrPrefsStore.resetToDefaults(context)
                                    KrkrPrefsSchema.ALL_BY_KEY.values.forEach { item ->
                                        values[item.key] = item.defaultAsString()
                                    }
                                    scope.launch { snackbarHostState.showSnackbar(text.resetDefaults) }
                                }) {
                                    Icon(Icons.Default.RestartAlt, contentDescription = text.resetDefaults)
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
                        Text(
                            text.renderSettingsHint,
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        KrkrPrefsSchema.ALL.forEach { section ->
                            SectionCard(
                                title = LocalizedPrefs.section(text, section.titleRes),
                            ) {
                                section.items.forEachIndexed { index, item ->
                                    if (index > 0) {
                                        HorizontalDivider(
                                            color = MaterialTheme.colorScheme.outlineVariant,
                                            thickness = 0.5.dp,
                                        )
                                    }
                                    PrefRow(item, values, onChange = { k, v ->
                                        values[k] = v
                                        // Persist immediately — small file, fast write.
                                        KrkrPrefsStore.update(context, mapOf(k to v))
                                    }, text = text)
                                }
                            }
                        }
                        Spacer(Modifier.height(24.dp))
                    }
                }
            }
        }
    }
}

@Composable
private fun SectionCard(title: String, content: @Composable () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surface,
        ),
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(
                title,
                style = MaterialTheme.typography.titleMedium,
                color = LauncherTokens.EngineAccent,
            )
            Spacer(Modifier.height(8.dp))
            content()
        }
    }
}

@Composable
private fun PrefRow(
    item: KrkrPrefsSchema.PrefItem,
    values: SnapshotStateMap<String, String>,
    onChange: (String, String) -> Unit,
    text: LauncherStrings.Texts,
) {
    val caption = LocalizedPrefs.caption(text, item.captionRes)
    when (item) {
        is KrkrPrefsSchema.PrefItem.Constant -> {
            // Section description: a paragraph, not a control.
            Text(
                caption,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(vertical = 8.dp),
            )
        }

        is KrkrPrefsSchema.PrefItem.Bool -> {
            val current = values[item.key]?.let { it == "1" || it.equals("true", true) } ?: item.default
            Row(
                modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    caption,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.widthIn(min = 0.dp).padding(end = 12.dp),
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                Spacer(Modifier.weight(1f))
                Switch(
                    checked = current,
                    onCheckedChange = { onChange(item.key, if (it) "1" else "0") },
                )
            }
        }

        is KrkrPrefsSchema.PrefItem.Select -> {
            val current = values[item.key] ?: item.default
            var menuOpen by remember(item.key) { mutableStateOf(false) }
            Row(
                modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    caption,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(end = 12.dp).weight(1f),
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                Column {
                    AssistChip(
                        onClick = { menuOpen = true },
                        label = {
                            val displayCaption = item.options.firstOrNull { it.second == current }
                                ?.first?.let { LocalizedPrefs.caption(text, it) } ?: current
                            Text(displayCaption)
                        },
                        colors = AssistChipDefaults.assistChipColors(
                            containerColor = MaterialTheme.colorScheme.surfaceVariant,
                            labelColor = MaterialTheme.colorScheme.onSurfaceVariant,
                        ),
                    )
                    DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                        item.options.forEach { (capRes, raw) ->
                            DropdownMenuItem(
                                text = { Text(LocalizedPrefs.caption(text, capRes)) },
                                onClick = {
                                    menuOpen = false
                                    onChange(item.key, raw)
                                },
                            )
                        }
                    }
                }
            }
        }

        is KrkrPrefsSchema.PrefItem.SliderFloat -> {
            val current = values[item.key]?.toFloatOrNull() ?: item.default
            Column(modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(caption, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                    Text("%.2f".format(current), color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                androidx.compose.material3.Slider(
                    value = current,
                    valueRange = item.min..item.max,
                    onValueChange = { onChange(item.key, it.toString()) },
                )
            }
        }

        is KrkrPrefsSchema.PrefItem.TextField -> {
            val current = values[item.key] ?: item.default
            OutlinedTextField(
                value = current,
                onValueChange = { onChange(item.key, it) },
                label = { Text(caption) },
                singleLine = true,
                modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
            )
        }
    }
}

private fun KrkrPrefsSchema.PrefItem.defaultAsString(): String = when (this) {
    is KrkrPrefsSchema.PrefItem.Bool -> if (default) "1" else "0"
    is KrkrPrefsSchema.PrefItem.Select -> default
    is KrkrPrefsSchema.PrefItem.SliderFloat -> default.toString()
    is KrkrPrefsSchema.PrefItem.TextField -> default
    is KrkrPrefsSchema.PrefItem.Constant -> ""
}

/**
 * Caption resolution for engine preference keys. Delegates to
 * [KrkrPrefsCaptions] which mirrors the C++ LocaleConfigManager strings;
 * unknown keys fall through to a humanised version of the raw key.
 */
private object LocalizedPrefs {
    fun section(text: LauncherStrings.Texts, key: String): String =
        resolve(text, key)

    fun caption(text: LauncherStrings.Texts, key: String): String =
        resolve(text, key)

    private fun resolve(text: LauncherStrings.Texts, key: String): String {
        // Detect active language by checking a known caption that differs
        // between en/zh tables (avoid plumbing language through every call).
        val lang = if (text.aboutTitle == "关于") LauncherPrefs.LANG_ZH else LauncherPrefs.LANG_EN
        return KrkrPrefsCaptions.resolve(lang, key)
    }
}
