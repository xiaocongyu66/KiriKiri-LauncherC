import 'package:flutter/material.dart';

ThemeData buildLauncherTheme(Brightness brightness) {
  final scheme = ColorScheme.fromSeed(
    seedColor: const Color(0xff386a6a),
    brightness: brightness,
  );
  final base = ThemeData(
    colorScheme: scheme,
    useMaterial3: true,
    fontFamily: 'NotoSansCJK',
  );
  return base.copyWith(
    scaffoldBackgroundColor: scheme.surfaceContainerLowest,
    appBarTheme: AppBarTheme(
      elevation: 0,
      scrolledUnderElevation: 0,
      centerTitle: false,
      backgroundColor: scheme.surface,
      foregroundColor: scheme.onSurface,
    ),
    cardTheme: CardThemeData(
      elevation: 0,
      margin: EdgeInsets.zero,
      color: scheme.surfaceContainerLow,
      surfaceTintColor: Colors.transparent,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
    ),
    listTileTheme: ListTileThemeData(
      iconColor: scheme.onSurfaceVariant,
      tileColor: Colors.transparent,
      titleTextStyle: base.textTheme.bodyLarge?.copyWith(color: scheme.onSurface),
      subtitleTextStyle: base.textTheme.bodySmall?.copyWith(color: scheme.onSurfaceVariant),
    ),
    navigationBarTheme: NavigationBarThemeData(
      elevation: 3,
      backgroundColor: scheme.surfaceContainer,
      indicatorColor: scheme.secondaryContainer,
      surfaceTintColor: Colors.transparent,
      shadowColor: Colors.transparent,
      labelBehavior: NavigationDestinationLabelBehavior.alwaysShow,
    ),
    navigationRailTheme: NavigationRailThemeData(
      backgroundColor: Colors.transparent,
      indicatorColor: scheme.secondaryContainer,
      selectedIconTheme: IconThemeData(color: scheme.onSecondaryContainer),
      selectedLabelTextStyle: base.textTheme.labelMedium?.copyWith(color: scheme.onSurface),
      unselectedLabelTextStyle: base.textTheme.labelMedium?.copyWith(color: scheme.onSurfaceVariant),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
        minimumSize: const Size(48, 44),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
        minimumSize: const Size(48, 44),
      ),
    ),
    iconButtonTheme: IconButtonThemeData(
      style: IconButton.styleFrom(
        tapTargetSize: MaterialTapTargetSize.shrinkWrap,
        visualDensity: VisualDensity.standard,
      ),
    ),
    chipTheme: base.chipTheme.copyWith(
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8), side: BorderSide(color: scheme.outlineVariant)),
      side: BorderSide(color: scheme.outlineVariant),
      backgroundColor: scheme.surfaceContainerLow,
      selectedColor: scheme.secondaryContainer,
      showCheckmark: false,
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: scheme.surfaceContainerHighest.withOpacity(0.55),
      border: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide.none),
      enabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide.none),
      focusedBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: scheme.primary, width: 1.4)),
    ),
  );
}
