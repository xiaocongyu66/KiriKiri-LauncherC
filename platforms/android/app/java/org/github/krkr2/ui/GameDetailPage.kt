package org.github.krkr2.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.outlined.Folder
import androidx.compose.material.icons.outlined.Inventory2
import androidx.compose.material.icons.outlined.Schedule
import androidx.compose.material.icons.outlined.Timer
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Divider
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import coil.request.ImageRequest
import org.github.krkr2.LauncherStrings
import org.github.krkr2.data.GameInfo
import org.github.krkr2.data.TimeFormat

/**
 * Game detail page — ported from KrKr2-Next's GameDetailPage.
 *
 * Layout:
 *   - Top hero: blurred cover background + centered small cover card + title
 *   - Bottom sheet (24dp top radius): info rows → launch button → manage section
 */
@Composable
fun GameDetailPage(
    game: GameInfo,
    text: LauncherStrings.Texts,
    onBack: () -> Unit,
    onLaunch: (GameInfo) -> Unit,
    onRename: (GameInfo, String) -> Unit,
) {
    val context = LocalContext.current
    var renameDialog by remember { mutableStateOf(false) }

    Scaffold { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(padding),
        ) {
            // === Top hero section ===
            TopHeroSection(game, text, onBack)

            // === Bottom sheet content ===
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(
                        MaterialTheme.colorScheme.surface,
                        RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp),
                    )
                    .padding(horizontal = 20.dp, vertical = 24.dp),
            ) {
                // Info rows
                InfoRow(Icons.Outlined.Folder, game.path)
                if (game.lastPlayedMillis > 0L) {
                    Spacer(Modifier.height(8.dp))
                    InfoRow(Icons.Outlined.Schedule, "${text.lastPlayed}: ${TimeFormat.relativeDate(game.lastPlayedMillis, text)}")
                }
                if (game.playDurationSeconds >= 60L) {
                    Spacer(Modifier.height(8.dp))
                    InfoRow(Icons.Outlined.Timer, "${text.playTime}: ${TimeFormat.playDuration(game.playDurationSeconds)}")
                }
                Spacer(Modifier.height(8.dp))
                InfoRow(Icons.Outlined.Inventory2, if (game.isXp3) text.xp3Archive else text.directory)

                // Launch button
                Spacer(Modifier.height(24.dp))
                Button(
                    onClick = { onLaunch(game) },
                    modifier = Modifier.fillMaxWidth().height(52.dp),
                ) {
                    Icon(Icons.Default.PlayArrow, null)
                    Spacer(Modifier.width(8.dp))
                    Text(text.launchGame, fontWeight = FontWeight.SemiBold)
                }

                // Manage section
                Spacer(Modifier.height(16.dp))
                Card(
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow),
                    shape = RoundedCornerShape(12.dp),
                ) {
                    ListItem(
                        headlineContent = { Text(text.rename) },
                        leadingContent = { Icon(Icons.Default.Edit, null) },
                        modifier = Modifier.fillMaxWidth().let { m ->
                            @Suppress("DEPRECATION")
                            m
                        },
                    )
                    // Use a simple divider approach
                    Box(Modifier.fillMaxWidth().height(1.dp).padding(start = 56.dp).background(MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f)))
                }
            }
        }
    }

    // Rename dialog
    if (renameDialog) {
        var newTitle by remember { mutableStateOf(game.title) }
        AlertDialog(
            onDismissRequest = { renameDialog = false },
            title = { Text(text.rename) },
            text = {
                OutlinedTextField(
                    value = newTitle,
                    onValueChange = { newTitle = it },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                )
            },
            confirmButton = {
                TextButton(onClick = { onRename(game, newTitle); renameDialog = false }) {
                    Text(text.save)
                }
            },
            dismissButton = {
                TextButton(onClick = { renameDialog = false }) { Text(text.cancel) }
            },
        )
    }
}

@Composable
private fun TopHeroSection(game: GameInfo, text: LauncherStrings.Texts, onBack: () -> Unit) {
    val colorScheme = MaterialTheme.colorScheme
    val context = LocalContext.current
    val coverPath = game.coverPath?.takeIf { it.isNotBlank() }

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(320.dp),
    ) {
        // Blurred background
        if (coverPath != null) {
            AsyncImage(
                model = ImageRequest.Builder(context).data(coverPath).crossfade(true).build(),
                contentDescription = null,
                contentScale = ContentScale.Crop,
                modifier = Modifier.fillMaxSize().blur(28.dp),
            )
            Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.35f)))
        } else {
            Box(
                Modifier.fillMaxSize().background(
                    Brush.linearGradient(listOf(colorScheme.surfaceContainerHigh, colorScheme.surfaceContainerHighest))
                )
            )
        }

        // Back button
        IconButton(
            onClick = onBack,
            modifier = Modifier
                .windowInsetsPadding(WindowInsets.statusBars)
                .padding(8.dp)
                .align(Alignment.TopStart),
        ) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null, tint = Color.White)
        }

        // Centered cover card + title
        Column(
            modifier = Modifier.align(Alignment.Center).padding(horizontal = 32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            // Small cover card (140w, 3:4 aspect, elevation 12)
            Card(
                elevation = CardDefaults.cardElevation(defaultElevation = 12.dp),
                shape = RoundedCornerShape(12.dp),
                modifier = Modifier.size(width = 140.dp, height = 187.dp),
            ) {
                if (coverPath != null) {
                    AsyncImage(
                        model = ImageRequest.Builder(context).data(coverPath).crossfade(true).build(),
                        contentDescription = null,
                        contentScale = ContentScale.Crop,
                        modifier = Modifier.fillMaxSize(),
                    )
                } else {
                    Box(
                        Modifier.fillMaxSize().background(colorScheme.surfaceContainerHigh),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            Icons.Outlined.Inventory2, null,
                            tint = colorScheme.primary.copy(alpha = 0.5f),
                            modifier = Modifier.size(48.dp),
                        )
                    }
                }
            }

            Spacer(Modifier.height(16.dp))

            Text(
                text = game.displayTitle,
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
                color = Color.White,
                textAlign = TextAlign.Center,
                maxLines = 3,
                overflow = TextOverflow.Ellipsis,
            )

            if (!game.developer.isNullOrBlank()) {
                Spacer(Modifier.height(6.dp))
                Text(
                    text = game.developer,
                    style = MaterialTheme.typography.bodyMedium,
                    color = Color.White.copy(alpha = 0.7f),
                    textAlign = TextAlign.Center,
                )
            }
        }
    }
}

@Composable
private fun InfoRow(icon: ImageVector, value: String) {
    val colorScheme = MaterialTheme.colorScheme
    Row(verticalAlignment = Alignment.CenterVertically) {
        Icon(icon, null, tint = colorScheme.onSurfaceVariant, modifier = Modifier.size(18.dp))
        Spacer(Modifier.width(10.dp))
        Text(
            text = value,
            style = MaterialTheme.typography.bodySmall,
            color = colorScheme.onSurfaceVariant,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}