package org.github.krkr2

import android.app.Activity
import android.os.Build
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * MD3-first launcher theme.
 *
 * Dynamic color is enabled on Android 12+ and falls back to a calm,
 * high-contrast static palette on older versions. We keep the launcher
 * accent in the warm orange family to echo the krkr/game identity, but
 * everything else follows Material 3 tonal surfaces.
 */
private val StaticDark = darkColorScheme(
    primary = Color(0xFFFFB59D),
    onPrimary = Color(0xFF4B1A08),
    primaryContainer = Color(0xFF6B2B13),
    onPrimaryContainer = Color(0xFFFFDBCF),
    secondary = Color(0xFFD8C2FF),
    onSecondary = Color(0xFF382A4A),
    secondaryContainer = Color(0xFF4F4061),
    onSecondaryContainer = Color(0xFFF0DEFF),
    tertiary = Color(0xFF9AD8E8),
    onTertiary = Color(0xFF053743),
    tertiaryContainer = Color(0xFF214D59),
    onTertiaryContainer = Color(0xFFBEEAF7),
    background = Color(0xFF121318),
    onBackground = Color(0xFFE4E1E8),
    surface = Color(0xFF121318),
    onSurface = Color(0xFFE4E1E8),
    surfaceVariant = Color(0xFF47464F),
    onSurfaceVariant = Color(0xFFC8C5CF),
    outline = Color(0xFF918F99),
    outlineVariant = Color(0xFF47464F),
    scrim = Color(0xFF000000),
)

private val StaticLight = lightColorScheme(
    primary = Color(0xFF8D4B35),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFFFDBCF),
    onPrimaryContainer = Color(0xFF361003),
    secondary = Color(0xFF645A70),
    onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFE8DEF8),
    onSecondaryContainer = Color(0xFF1F182A),
    tertiary = Color(0xFF4F6974),
    onTertiary = Color(0xFFFFFFFF),
    tertiaryContainer = Color(0xFFCDE7F1),
    onTertiaryContainer = Color(0xFF061F29),
    background = Color(0xFFFDF7FF),
    onBackground = Color(0xFF1D1B20),
    surface = Color(0xFFFDF7FF),
    onSurface = Color(0xFF1D1B20),
    surfaceVariant = Color(0xFFE7E0EB),
    onSurfaceVariant = Color(0xFF49454E),
    outline = Color(0xFF7A757F),
    outlineVariant = Color(0xFFCAC4CF),
    scrim = Color(0xFF000000),
)

private val LauncherTypography = Typography(
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
    val context = LocalContext.current
    val scheme = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        if ((context as? Activity) != null) dynamicDarkColorScheme(context) else StaticDark
    } else {
        StaticDark
    }
    MaterialTheme(
        colorScheme = scheme,
        typography = LauncherTypography,
        content = content,
    )
}

object LauncherTokens {
    val EngineAccent = Color(0xFF4F6974)
    val OverrideAccent = Color(0xFF8D4B35)
    val Dim = Color(0xFF8E8E99)
}
