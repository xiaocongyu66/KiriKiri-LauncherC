package org.github.krkr2.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * KrKr2 Launcher theme.
 *
 * Visual language is inspired by KrKr2-Next (Flutter): Material 3 with a pink
 * seed color, dark mode by default, 12dp Card corners, low-elevation tinted
 * surfaces, primary-colored section headers.
 *
 * We avoid dynamic color (Android 12+ Material You) on purpose to keep the
 * launcher looking the same across vendors (vivo/MIUI/etc. can otherwise
 * skew the tint).
 */

// Seed color: Material's Pink 400 family, matches KrKr2-Next's Colors.pink seed.
private val SeedPink = Color(0xFFE91E63)

private val DarkScheme = darkColorScheme(
    primary = Color(0xFFFFB1C8),
    onPrimary = Color(0xFF5E1133),
    primaryContainer = Color(0xFF7B2949),
    onPrimaryContainer = Color(0xFFFFD9E2),
    secondary = Color(0xFFE3BDC6),
    onSecondary = Color(0xFF422931),
    secondaryContainer = Color(0xFF5B3F47),
    onSecondaryContainer = Color(0xFFFFD9E2),
    tertiary = Color(0xFFEFBD94),
    onTertiary = Color(0xFF48290C),
    background = Color(0xFF181114),
    onBackground = Color(0xFFEFDFE2),
    surface = Color(0xFF181114),
    onSurface = Color(0xFFEFDFE2),
    surfaceVariant = Color(0xFF514347),
    onSurfaceVariant = Color(0xFFD5C2C6),
    surfaceTint = Color(0xFFFFB1C8),
    outline = Color(0xFF9D8C90),
    outlineVariant = Color(0xFF514347),
    error = Color(0xFFFFB4AB),
    onError = Color(0xFF690005),
    errorContainer = Color(0xFF93000A),
    onErrorContainer = Color(0xFFFFDAD6),
    inverseSurface = Color(0xFFEFDFE2),
    inverseOnSurface = Color(0xFF362E30),
    inversePrimary = Color(0xFF9A4061),
    surfaceContainerLowest = Color(0xFF120C0E),
    surfaceContainerLow = Color(0xFF20191B),
    surfaceContainer = Color(0xFF241D1F),
    surfaceContainerHigh = Color(0xFF2F2729),
    surfaceContainerHighest = Color(0xFF3A3234),
)

private val LightScheme = lightColorScheme(
    primary = Color(0xFF9A4061),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFFFD9E2),
    onPrimaryContainer = Color(0xFF3F001E),
    secondary = Color(0xFF75565F),
    onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFFFD9E2),
    onSecondaryContainer = Color(0xFF2C151D),
    tertiary = Color(0xFF7C5635),
    onTertiary = Color(0xFFFFFFFF),
    background = Color(0xFFFFF8F8),
    onBackground = Color(0xFF22191B),
    surface = Color(0xFFFFF8F8),
    onSurface = Color(0xFF22191B),
    surfaceVariant = Color(0xFFF2DDE1),
    onSurfaceVariant = Color(0xFF514347),
    surfaceTint = Color(0xFF9A4061),
    outline = Color(0xFF847377),
    outlineVariant = Color(0xFFD5C2C6),
    error = Color(0xFFBA1A1A),
    onError = Color(0xFFFFFFFF),
    errorContainer = Color(0xFFFFDAD6),
    onErrorContainer = Color(0xFF410002),
    inverseSurface = Color(0xFF382E30),
    inverseOnSurface = Color(0xFFFEEDEF),
    inversePrimary = Color(0xFFFFB1C8),
    surfaceContainerLowest = Color(0xFFFFFFFF),
    surfaceContainerLow = Color(0xFFFCF0F2),
    surfaceContainer = Color(0xFFF7EAEC),
    surfaceContainerHigh = Color(0xFFF1E4E7),
    surfaceContainerHighest = Color(0xFFEBDEE1),
)

private val KrKr2Typography = Typography(
    headlineMedium = TextStyle(
        fontSize = 28.sp,
        fontWeight = FontWeight.Bold,
        letterSpacing = 0.sp,
    ),
    titleLarge = TextStyle(
        fontSize = 22.sp,
        fontWeight = FontWeight.SemiBold,
        letterSpacing = 0.sp,
    ),
    titleSmall = TextStyle(
        fontSize = 14.sp,
        fontWeight = FontWeight.SemiBold,
        letterSpacing = 0.1.sp,
    ),
)

@Composable
fun KrKr2Theme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    forceDark: Boolean = true,
    content: @Composable () -> Unit,
) {
    // Force dark by default (KrKr2-Next does the same); flip when user opts out.
    val scheme = if (forceDark || darkTheme) DarkScheme else LightScheme
    MaterialTheme(
        colorScheme = scheme,
        typography = KrKr2Typography,
        content = content,
    )
}

// Compatibility alias so older call sites in MainActivity / SDLActivity still compile.
@Composable
fun LauncherTheme(content: @Composable () -> Unit) {
    KrKr2Theme(content = content)
}
