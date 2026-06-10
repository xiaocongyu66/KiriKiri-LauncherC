import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import 'src/bridge/launcher_bridge.dart';
import 'src/pages/game_overlay_page.dart';
import 'src/pages/launcher_home_page.dart';
import 'src/theme/launcher_theme.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const KiriKiriLauncherApp());
}

class KiriKiriLauncherApp extends StatelessWidget {
  const KiriKiriLauncherApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'KiriKiri Launcher',
      debugShowCheckedModeBanner: false,
      theme: buildLauncherTheme(Brightness.light),
      darkTheme: buildLauncherTheme(Brightness.dark),
      themeMode: ThemeMode.system,
      localizationsDelegates: const [
        GlobalMaterialLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
      ],
      supportedLocales: const [Locale('en'), Locale('zh')],
      routes: {
        '/': (_) => LauncherHomePage(bridge: LauncherBridge.instance),
        '/game-overlay': (_) => GameOverlayPage(bridge: LauncherBridge.instance),
      },
    );
  }
}
