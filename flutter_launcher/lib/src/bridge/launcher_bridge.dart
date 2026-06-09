import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

import '../models/game_entry.dart';
import '../models/game_menu_item.dart';

typedef _GetMainMenuJsonNative = Pointer<Utf8> Function();
typedef _ActivateMenuItemNative = Int32 Function(Pointer<Utf8> path);
typedef _ActivateMenuItemDart = int Function(Pointer<Utf8> path);
typedef _LaunchGameNative = Int32 Function(Pointer<Utf8> path);
typedef _LaunchGameDart = int Function(Pointer<Utf8> path);

class LauncherBridge {
  LauncherBridge({DynamicLibrary? library}) : _providedLibrary = library;

  static final LauncherBridge instance = LauncherBridge();

  final DynamicLibrary? _providedLibrary;

  late final DynamicLibrary _library = _providedLibrary ?? _openLibrary();

  late final _getMainMenuJson = _library
      .lookupFunction<_GetMainMenuJsonNative, _GetMainMenuJsonNative>(
        'KR2LauncherGetMainMenuJson',
      );

  late final _activateMenuItem = _library
      .lookupFunction<_ActivateMenuItemNative, _ActivateMenuItemDart>(
        'KR2LauncherActivateMenuItem',
      );

  late final _launchGame = _library.lookupFunction<_LaunchGameNative, _LaunchGameDart>(
    'KR2LauncherLaunchGame',
  );

  Future<List<GameEntry>> scanGames() async {
    return const [
      GameEntry(
        title: 'Select a KiriKiri game',
        subtitle: 'Native game scanner C API is pending',
        path: '',
      ),
    ];
  }

  Future<void> pickGame() async {}

  Future<void> launchGame(GameEntry game) async {
    final path = game.path.toNativeUtf8();
    try {
      final result = _launchGame(path);
      if (result != 0) {
        throw StateError('KR2LauncherLaunchGame failed: $result');
      }
    } finally {
      calloc.free(path);
    }
  }

  Future<void> openSettings() async {}

  Future<void> openDiagnostics() async {}

  Future<List<GameMenuItem>> getMainMenu() async {
    final pointer = _getMainMenuJson();
    if (pointer == nullptr) {
      return const [];
    }
    final decoded = jsonDecode(pointer.toDartString());
    if (decoded is! List) {
      return const [];
    }
    return decoded
        .whereType<Map>()
        .map((item) => GameMenuItem.fromMap(item.cast<String, Object?>()))
        .toList(growable: false);
  }

  Future<void> activateMenuItem(GameMenuItem item) async {
    final path = item.path.toNativeUtf8();
    try {
      final result = _activateMenuItem(path);
      if (result != 0) {
        throw StateError('KR2LauncherActivateMenuItem failed: $result');
      }
    } finally {
      calloc.free(path);
    }
  }

  static DynamicLibrary _openLibrary() {
    if (Platform.isAndroid || Platform.isLinux) {
      return DynamicLibrary.open('libkrkr2.so');
    }
    if (Platform.isWindows) {
      return DynamicLibrary.open('krkr2.dll');
    }
    if (Platform.isMacOS || Platform.isIOS) {
      return DynamicLibrary.process();
    }
    return DynamicLibrary.process();
  }
}
