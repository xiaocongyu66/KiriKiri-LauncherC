import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter/services.dart';

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
  static const MethodChannel _platformChannel = MethodChannel('org.github.krkr2/platform');

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

  Future<List<GameEntry>> scanGames({String rootPath = '/sdcard', int maxDepth = 3}) async {
    final root = Directory(rootPath.trim().isEmpty ? '/sdcard' : rootPath.trim());
    if (!root.existsSync()) {
      return const [];
    }
    final games = <GameEntry>[];
    await _scanDirectory(root, 0, maxDepth, games);
    games.sort((left, right) => left.title.toLowerCase().compareTo(right.title.toLowerCase()));
    return games;
  }

  Future<void> _scanDirectory(Directory directory, int depth, int maxDepth, List<GameEntry> games) async {
    if (depth > maxDepth) {
      return;
    }
    List<FileSystemEntity> entries;
    try {
      entries = directory.listSync(followLinks: false);
    } on FileSystemException {
      return;
    }

    final hasGameScript = entries.whereType<File>().any((file) {
      final name = _basename(file.path).toLowerCase();
      return name == 'data.xp3' || name == 'startup.tjs';
    });
    if (hasGameScript) {
      games.add(GameEntry(title: _basename(directory.path), subtitle: directory.path, path: directory.path));
      return;
    }

    for (final child in entries.whereType<Directory>()) {
      final name = _basename(child.path);
      if (name.startsWith('.')) {
        continue;
      }
      await _scanDirectory(child, depth + 1, maxDepth, games);
    }
  }

  String _basename(String path) {
    final normalized = path.replaceAll('\\', '/');
    final trimmed = normalized.endsWith('/') ? normalized.substring(0, normalized.length - 1) : normalized;
    final index = trimmed.lastIndexOf('/');
    if (index < 0) {
      return trimmed;
    }
    return trimmed.substring(index + 1);
  }

  Future<void> pickGame() async {
    await _platformChannel.invokeMethod<void>('pickGameRoot');
  }

  Future<void> requestFileManagementPermission() async {
    await _platformChannel.invokeMethod<void>('requestFileManagementPermission');
  }

  Future<bool> hasFileManagementPermission() async {
    final granted = await _platformChannel.invokeMethod<bool>('hasFileManagementPermission');
    return granted ?? false;
  }

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

  Future<void> openSettings() async {
    await _platformChannel.invokeMethod<void>('openSettings');
  }

  Future<void> openDiagnostics() async {
    await _platformChannel.invokeMethod<void>('openDiagnostics');
  }

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
