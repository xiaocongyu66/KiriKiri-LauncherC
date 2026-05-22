package org.github.krkr2

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * Gaming-launcher dark theme.
 *
 * Style anchor: "Dark & Elegant + Gaming". Engine assets (krkr / emoteplayer)
 * lean on warm orange-red, so we use that as primary; cyan tertiary picks up
 * the rendering / engine config sections so they read as "system" not as
 * primary actions.
 *
 * Surfaces are stacked with explicit colors instead of pure tonal elevation
 * because the launcher mixes high-contrast game cards with subtle settings
 * surfaces — explicit values give predictable cards on top of a near-black
 * background.
 */
private val GamingDark = darkColorScheme(
    // Primary — krkr orange. Used for FAB, primary buttons, highlight chips.
    primary = Color(0xFFFF7043),
    onPrimary = Color(0xFF1B0900),
    primaryContainer = Color(0xFF7A2900),
    onPrimaryContainer = Color(0xFFFFDBCB),

    // Secondary — muted lavender for selection / accents.
    secondary = Color(0xFFCDB7E5),
    onSecondary = Color(0xFF332644),
    secondaryContainer = Color(0xFF483C5C),
    onSecondaryContainer = Color(0xFFEADDFF),

    // Tertiary — cyan, reserved for engine / render settings cards.
    tertiary = Color(0xFF80DEEA),
    onTertiary = Color(0xFF003640),
    tertiaryContainer = Color(0xFF005662),
    onTertiaryContainer = Color(0xFFB2EBF2),

    // Surfaces — near-black background, slightly lifted surfaces.
    background = Color(0xFF0A0A0F),
    onBackground = Color(0xFFE6E1E5),
    surface = Color(0xFF13131A),
    onSurface = Color(0xFFE6E1E5),
    surfaceVariant = Color(0xFF1F1F28),
    onSurfaceVariant = Color(0xFFC8C5D0),
    surfaceTint = Color(0xFFFF7043),

    // Errors stay in the red family but a touch warmer to match primary.
    error = Color(0xFFFFB4AB),
    onError = Color(0xFF690005),
    errorContainer = Color(0xFF93000A),
    onErrorContainer = Color(0xFFFFDAD6),

    outline = Color(0xFF2A2A35),
    outlineVariant = Color(0xFF3A3A48),
    scrim = Color(0xFF000000),
)

/**
 * Typography aligned to M3 spec but with slightly tightened display weights
 * for the gaming feel. We keep Roboto so the system bundles the font, not
 * our APK.
 */
private val GamingTypography = Typography(
    displayLarge = TextStyle(fontSize = 48.sp, fontWeight = FontWeight.Bold, lineHeight = 56.sp, letterSpacing = (-0.25).sp),
    displayMedium = TextStyle(fontSize = 36.sp, fontWeight = FontWeight.SemiBold, lineHeight = 44.sp),
    headlineLarge = TextStyle(fontSize = 28.sp, fontWeight = FontWeight.SemiBold, lineHeight = 36.sp),
    headlineMedium = TextStyle(fontSize = 24.sp, fontWeight = FontWeight.SemiBold, lineHeight = 32.sp),
    headlineSmall = TextStyle(fontSize = 20.sp, fontWeight = FontWeight.SemiBold, lineHeight = 28.sp),
    titleLarge = TextStyle(fontSize = 20.sp, fontWeight = FontWeight.Medium, lineHeight = 28.sp),
    titleMedium = TextStyle(fontSize = 16.sp, fontWeight = FontWeight.Medium, lineHeight = 24.sp, letterSpacing = 0.15.sp),
    titleSmall = TextStyle(fontSize = 14.sp, fontWeight = FontWeight.Medium, lineHeight = 20.sp, letterSpacing = 0.1.sp),
    bodyLarge = TextStyle(fontSize = 16.sp, lineHeight = 24.sp, letterSpacing = 0.5.sp),
    bodyMedium = TextStyle(fontSize = 14.sp, lineHeight = 20.sp, letterSpacing = 0.25.sp),
    bodySmall = TextStyle(fontSize = 12.sp, lineHeight = 16.sp, letterSpacing = 0.4.sp),
    labelLarge = TextStyle(fontSize = 14.sp, fontWeight = FontWeight.Medium, lineHeight = 20.sp, letterSpacing = 0.1.sp),
    labelMedium = TextStyle(fontSize = 12.sp, fontWeight = FontWeight.Medium, lineHeight = 16.sp, letterSpacing = 0.5.sp),
    labelSmall = TextStyle(fontSize = 11.sp, fontWeight = FontWeight.Medium, lineHeight = 16.sp, letterSpacing = 0.5.sp),
)

@Composable
fun LauncherTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = GamingDark,
        typography = GamingTypography,
        content = content,
    )
}

/**
 * Color tokens that don't exist in M3 but the launcher uses repeatedly.
 * Centralised here so we don't sprinkle hex values across screens.
 */
object LauncherTokens {
    // Engine / render settings accent — matches tertiary so render cards
    // read as "system" sections without changing the M3 colorScheme calls.
    val EngineAccent = Color(0xFF80DEEA)

    // Per-game override accent — different from engine to make it clear
    // that the value shadows global. Warm yellow-amber.
    val OverrideAccent = Color(0xFFFFC857)

    // Dim disclosed state used by the diagnostics chips.
    val Dim = Color(0xFF8E8E99)
}
