import 'package:flutter/material.dart';

ThemeData buildLauncherTheme(Brightness brightness) {
  final scheme = ColorScheme.fromSeed(
    seedColor: const Color(0xff7c4dff),
    brightness: brightness,
  );
  return ThemeData(
    colorScheme: scheme,
    useMaterial3: true,
    fontFamily: 'NotoSansCJK',
    cardTheme: CardThemeData(
      elevation: 0,
      color: scheme.surfaceContainerHighest.withOpacity(0.64),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(24)),
    ),
  );
}
