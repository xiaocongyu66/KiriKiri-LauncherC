package org.github.krkr2.ui.components

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.VideogameAsset
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import coil.request.ImageRequest
import org.github.krkr2.data.GameInfo

/**
 * The launcher's signature game tile, designed to mirror KrKr2-Next's
 * `_CoverCard`:
 *
 *   Card (12dp corners, elev=1)
 *   └── Stack (3 : 4 aspect)
 *       ├── cover image (BoxFit.cover) or muted gradient placeholder
 *       ├── bottom-aligned dark vertical gradient (last 80dp goes to black54)
 *       ├── bottom-left overlay: title (white, 13sp, w600) + subtitle
 *       └── tap / long-press for context menu
 *
 * The aspect ratio of 3 : 4 (height = 4/3 * width) is enforced by the parent
 * SliverGrid in the original; we do the same here.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun CoverCard(
    game: GameInfo,
    subtitle: String?,
    onClick: () -> Unit,
    onLongPress: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val colorScheme = MaterialTheme.colorScheme
    val context = LocalContext.current
    Card(
        modifier = modifier,
        shape = RoundedCornerShape(12.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp),
        colors = CardDefaults.cardColors(containerColor = colorScheme.surfaceContainerHigh),
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .combinedClickable(
                    onClick = onClick,
                    onLongClick = onLongPress,
                ),
        ) {
            // 1) Background: cover image or gradient placeholder.
            val coverModel = game.coverPath?.takeIf { it.isNotBlank() }
            if (coverModel != null) {
                AsyncImage(
                    model = ImageRequest.Builder(context)
                        .data(coverModel)
                        .crossfade(true)
                        .build(),
                    contentDescription = null,
                    contentScale = ContentScale.Crop,
                    modifier = Modifier.fillMaxSize(),
                )
            } else {
                CoverPlaceholder()
            }

            // 2) Bottom dark gradient overlay for legibility (80dp tall).
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(96.dp)
                    .align(Alignment.BottomCenter)
                    .background(
                        Brush.verticalGradient(
                            colors = listOf(
                                Color.Transparent,
                                Color(0x8A000000),
                            ),
                        ),
                    ),
            )

            // 3) Title + subtitle.
            Column(
                modifier = Modifier
                    .align(Alignment.BottomStart)
                    .padding(start = 12.dp, end = 12.dp, bottom = 10.dp),
            ) {
                Text(
                    text = game.displayTitle,
                    color = Color.White,
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 13.sp,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    lineHeight = 16.sp,
                )
                if (!subtitle.isNullOrBlank()) {
                    Spacer(Modifier.height(2.dp))
                    Text(
                        text = subtitle,
                        color = Color.White.copy(alpha = 0.6f),
                        fontSize = 11.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
    }
}

@Composable
private fun CoverPlaceholder() {
    val colorScheme = MaterialTheme.colorScheme
    Box(
        modifier = Modifier
            .fillMaxSize()
            .clip(RoundedCornerShape(0.dp))
            .background(
                Brush.linearGradient(
                    colors = listOf(
                        colorScheme.surfaceContainerHigh,
                        colorScheme.surfaceContainerHighest,
                    ),
                ),
            ),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            imageVector = Icons.Outlined.VideogameAsset,
            contentDescription = null,
            tint = colorScheme.primary.copy(alpha = 0.6f),
            modifier = Modifier.size(48.dp),
        )
    }
}