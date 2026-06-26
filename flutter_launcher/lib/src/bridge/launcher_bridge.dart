import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';

import 'package:ffi/ffi.dart';
import 'package:flutter/services.dart';

import '../models/game_entry.dart';
import '../models/game_menu_item.dart';

typedef _GetMainMenuJsonNative = Pointer<Utf8> Function();
typedef _ActivateMenuItemNative = Int32 Function(Pointer<Utf8> path);
typedef _ActivateMenuItemDart = int Function(Pointer<Utf8> path);
typedef _LaunchGameNative = Int32 Function(Pointer<Utf8> path);
typedef _LaunchGameDart = int Function(Pointer<Utf8> path);
typedef _PerformOverlayActionNative = Int32 Function(Pointer<Utf8> actionName);
typedef _PerformOverlayActionDart = int Function(Pointer<Utf8> actionName);

class LauncherBridge {
  LauncherBridge({DynamicLibrary? library}) : _providedLibrary = library;

  static final LauncherBridge instance = LauncherBridge();
  static const MethodChannel _platformChannel = MethodChannel('org.github.krkr2/platform');

  static const _gameMarkers = {
    'startup.tjs',
    'start.tjs',
    'data.xp3',
    'patch.xp3',
    'scenario.ks',
    'first.ks',
    'config.tjs',
  };
  static const _imageExts = {'jpg', 'jpeg', 'png', 'webp'};
  static const _coverNames = ['cover', 'icon', 'title', 'thumb', 'thumbnail', 'package', 'bg', 'background', 'main'];
  static const _launchExts = {'xp3', 'tjs', 'ks'};
  static const _preferredLaunchNames = [
    'startup.tjs',
    'start.tjs',
    'data.xp3',
    'startup.xp3',
    'start.xp3',
    'main.xp3',
    'game.xp3',
    'first.ks',
    'scenario.ks',
  ];

  final DynamicLibrary? _providedLibrary;

  late final DynamicLibrary _library = _providedLibrary ?? _openLibrary();

  late final _getMainMenuJson = _library.lookupFunction<_GetMainMenuJsonNative, _GetMainMenuJsonNative>(
    'KR2LauncherGetMainMenuJson',
  );

  late final _activateMenuItem = _library.lookupFunction<_ActivateMenuItemNative, _ActivateMenuItemDart>(
    'KR2LauncherActivateMenuItem',
  );

  late final _launchGame = _library.lookupFunction<_LaunchGameNative, _LaunchGameDart>(
    'KR2LauncherLaunchGame',
  );

  late final _performOverlayAction = _library.lookupFunction<_PerformOverlayActionNative, _PerformOverlayActionDart>(
    'KR2LauncherPerformOverlayAction',
  );

  Future<List<GameEntry>> scanGames({String rootPath = '/storage/emulated/0/krkr2pro', int maxDepth = 2}) {
    return Isolate.run(() => _scanGamesSync(rootPath, maxDepth));
  }

  Future<String> getGameRoot() async {
    try {
      final root = await _platformChannel.invokeMethod<String>('getGameRoot');
      return root?.trim().isNotEmpty == true ? root! : '/storage/emulated/0/krkr2pro';
    } on MissingPluginException {
      return '/storage/emulated/0/krkr2pro';
    }
  }

  Future<void> setGameRoot(String path) async {
    await _platformChannel.invokeMethod<void>('setGameRoot', {'path': path});
  }

  Future<int> getScanDepth() async {
    try {
      final depth = await _platformChannel.invokeMethod<int>('getScanDepth');
      return (depth ?? 2).clamp(1, 10).toInt();
    } on MissingPluginException {
      return 2;
    }
  }

  static List<GameEntry> _scanGamesSync(String rootPath, int maxDepth) {
    final root = Directory(rootPath.trim().isEmpty ? '/storage/emulated/0/krkr2pro' : rootPath.trim());
    if (!root.existsSync()) {
      return const [];
    }
    final result = <String, GameEntry>{};
    _scanDir(root, 0, maxDepth.clamp(0, 32).toInt(), result);
    final games = result.values.toList(growable: false);
    games.sort((left, right) {
      final byModified = right.lastModified.compareTo(left.lastModified);
      if (byModified != 0) {
        return byModified;
      }
      return left.title.toLowerCase().compareTo(right.title.toLowerCase());
    });
    return games;
  }

  Future<List<String>> listLaunchCandidates(String gameDir) async {
    final root = Directory(gameDir);
    if (!root.existsSync()) {
      return const [];
    }
    final files = <File>[];
    _walkLaunchCandidates(root, root, files);
    files.sort((left, right) {
      final rank = _launchRank(left, root).compareTo(_launchRank(right, root));
      if (rank != 0) {
        return rank;
      }
      return _relativePath(left, root).toLowerCase().compareTo(_relativePath(right, root).toLowerCase());
    });
    return files.take(80).map((file) => file.path).toList(growable: false);
  }

  static void _scanDir(Directory dir, int depth, int maxDepth, Map<String, GameEntry> result) {
    if (depth > maxDepth || _basename(dir.path).startsWith('.')) {
      return;
    }
    List<FileSystemEntity> children;
    try {
      children = dir.listSync(followLinks: false);
    } on FileSystemException {
      return;
    }
    if (children.isEmpty) {
      return;
    }
    if (_isGameDir(dir, children)) {
      final entry = _buildGameEntry(dir, children);
      result[entry.path] = entry;
      return;
    }
    for (final child in children.whereType<Directory>()) {
      if (!_basename(child.path).startsWith('.')) {
        _scanDir(child, depth + 1, maxDepth, result);
      }
    }
  }

  static bool _isGameDir(Directory dir, List<FileSystemEntity> children) {
    final names = children.map((child) => _basename(child.path).toLowerCase()).toSet();
    if (_gameMarkers.any(names.contains)) {
      return true;
    }
    if (children.whereType<File>().any((file) => _extension(file.path) == 'xp3')) {
      return true;
    }
    if (children.whereType<File>().any((file) => _extension(file.path) == 'ks')) {
      return true;
    }
    return Directory('${dir.path}/data').existsSync() && Directory('${dir.path}/scenario').existsSync();
  }

  static GameEntry _buildGameEntry(Directory dir, List<FileSystemEntity> children) {
    final title = _readTitle(dir, children);
    final images = _collectImages(dir, children);
    final cover = _chooseCover(images);
    final background = _chooseBackground(images);
    final launch = _chooseLaunchFile(children.whereType<File>().toList(growable: false));
    final latest = children.fold<int>(dir.statSync().modified.millisecondsSinceEpoch, (last, child) {
      try {
        final modified = child.statSync().modified.millisecondsSinceEpoch;
        return modified > last ? modified : last;
      } on FileSystemException {
        return last;
      }
    });
    return GameEntry(
      title: title,
      path: dir.path,
      subtitle: dir.path,
      coverPath: cover?.path,
      backgroundPath: background?.path,
      launchFile: launch?.path,
      lastModified: latest,
    );
  }

  static String _readTitle(Directory dir, List<FileSystemEntity> children) {
    final titleFile = _firstFileWhere(children, (file) {
      final name = _basename(file.path).toLowerCase();
      return name == 'title.txt' || name == 'game.txt';
    });
    final fromFile = _readFirstNonBlankLine(titleFile);
    if (fromFile != null) {
      return fromFile;
    }
    final infoFile = _firstFileWhere(children, (file) {
      final name = _basename(file.path).toLowerCase();
      return name == 'package.json' || name == 'info.json';
    });
    final jsonText = _safeReadText(infoFile);
    if (jsonText != null) {
      final match = RegExp(r'"(?:title|name)"\s*:\s*"([^"]+)"').firstMatch(jsonText);
      final name = match?.group(1)?.trim();
      if (name != null && name.isNotEmpty) {
        return name;
      }
    }
    final fallback = _basename(dir.path).replaceAll('_', ' ').replaceAll('-', ' ').trim();
    return fallback.isEmpty ? dir.path : fallback;
  }

  static List<File> _collectImages(Directory dir, List<FileSystemEntity> children) {
    final direct = children.whereType<File>().where((file) => _imageExts.contains(_extension(file.path))).toList();
    const commonFolders = ['image', 'images', 'bg', 'bgimage', 'background', 'system', 'title', 'ui'];
    final nested = <File>[];
    for (final folder in commonFolders) {
      final target = Directory('${dir.path}/$folder');
      try {
        nested.addAll(target.listSync(followLinks: false).whereType<File>().where((file) => _imageExts.contains(_extension(file.path))));
      } on FileSystemException {
        continue;
      }
    }
    final byPath = <String, File>{};
    for (final image in [...direct, ...nested]) {
      byPath[image.path] = image;
    }
    return byPath.values.toList(growable: false);
  }

  static File? _chooseCover(List<File> images) {
    if (images.isEmpty) {
      return null;
    }
    for (final image in images) {
      final base = _nameWithoutExtension(image.path).toLowerCase();
      if (_coverNames.any((name) => base == name || base.contains(name))) {
        return image;
      }
    }
    return _largestFile(images);
  }

  static File? _chooseBackground(List<File> images) {
    if (images.isEmpty) {
      return null;
    }
    for (final image in images) {
      final base = _nameWithoutExtension(image.path).toLowerCase();
      if (base.contains('bg') || base.contains('back') || base.contains('title')) {
        return image;
      }
    }
    return _largestFile(images);
  }

  static File? _chooseLaunchFile(List<File> files) {
    final candidates = files.where((file) => _launchExts.contains(_extension(file.path))).toList();
    if (candidates.isEmpty) {
      return null;
    }
    candidates.sort((left, right) {
      final rank = _launchRank(left).compareTo(_launchRank(right));
      if (rank != 0) {
        return rank;
      }
      return _basename(left.path).toLowerCase().compareTo(_basename(right.path).toLowerCase());
    });
    return candidates.first;
  }

  static void _walkLaunchCandidates(Directory root, Directory dir, List<File> files) {
    List<FileSystemEntity> children;
    try {
      children = dir.listSync(followLinks: false);
    } on FileSystemException {
      return;
    }
    for (final child in children) {
      if (child is File && _launchExts.contains(_extension(child.path)) && _relativeDepth(child, root) <= 3) {
        files.add(child);
      } else if (child is Directory && !_basename(child.path).startsWith('.') && _relativeDepth(child, root) < 3) {
        _walkLaunchCandidates(root, child, files);
      }
    }
  }

  static int _launchRank(File file, [Directory? root]) {
    final name = _basename(file.path).toLowerCase();
    final preferredIndex = _preferredLaunchNames.indexOf(name);
    if (preferredIndex >= 0) {
      return preferredIndex;
    }
    final ext = _extension(file.path);
    final base = _nameWithoutExtension(file.path).toLowerCase();
    final baseRank = switch (ext) {
      'xp3' => base == 'boot'
          ? 20
          : (base == 'main' || base == 'game' || base == 'scenario' || base == 'script')
              ? 30
              : base.startsWith('data')
                  ? 40
                  : _isAssetArchiveBase(base)
                      ? 300
                      : 80,
      'tjs' => (base == 'main' || base == 'boot' || base == 'game') ? 60 : 90,
      'ks' => (base == 'first' || base == 'scenario') ? 70 : 100,
      _ => 500,
    };
    return baseRank + (root == null ? 0 : _relativeDepth(file, root) * 20);
  }

  static bool _isAssetArchiveBase(String base) {
    return base == 'patch' ||
        base.startsWith('patch') ||
        base == 'bg' ||
        base.startsWith('bg') ||
        base.contains('image') ||
        base.contains('voice') ||
        base.contains('sound') ||
        base.contains('audio') ||
        base.contains('music') ||
        base.contains('movie') ||
        base.contains('video') ||
        base.contains('effect');
  }

  static File? _largestFile(List<File> files) {
    files.sort((left, right) {
      int leftLength = 0;
      int rightLength = 0;
      try {
        leftLength = left.lengthSync();
      } on FileSystemException {}
      try {
        rightLength = right.lengthSync();
      } on FileSystemException {}
      return rightLength.compareTo(leftLength);
    });
    return files.first;
  }

  static String? _readFirstNonBlankLine(File? file) {
    final text = _safeReadText(file);
    if (text == null) {
      return null;
    }
    for (final line in const LineSplitter().convert(text)) {
      final trimmed = line.trim();
      if (trimmed.isNotEmpty) {
        return trimmed;
      }
    }
    return null;
  }

  static File? _firstFileWhere(List<FileSystemEntity> children, bool Function(File file) test) {
    for (final file in children.whereType<File>()) {
      if (test(file)) {
        return file;
      }
    }
    return null;
  }

  static String? _safeReadText(File? file) {
    if (file == null) {
      return null;
    }
    try {
      return file.readAsStringSync();
    } on FileSystemException {
      return null;
    }
  }

  static int _relativeDepth(FileSystemEntity entity, Directory root) {
    final relative = _relativePath(entity, root);
    if (relative.isEmpty) {
      return 0;
    }
    return '/'.allMatches(relative).length;
  }

  static String _relativePath(FileSystemEntity entity, Directory root) {
    final path = entity.path.replaceAll('\\', '/');
    final rootPath = root.path.replaceAll('\\', '/');
    if (path.startsWith('$rootPath/')) {
      return path.substring(rootPath.length + 1);
    }
    return _basename(path);
  }

  static String _extension(String path) {
    final name = _basename(path);
    final index = name.lastIndexOf('.');
    if (index < 0 || index == name.length - 1) {
      return '';
    }
    return name.substring(index + 1).toLowerCase();
  }

  static String _nameWithoutExtension(String path) {
    final name = _basename(path);
    final index = name.lastIndexOf('.');
    return index < 0 ? name : name.substring(0, index);
  }

  static String _basename(String path) {
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
    if (Platform.isAndroid) {
      try {
        await _platformChannel.invokeMethod<void>('launchGame', {
          'gameDir': game.path,
          'launchFile': game.launchFile ?? '',
          'title': game.title,
        });
        return;
      } on MissingPluginException {
        // Desktop/tests keep using the C ABI fallback below.
      } on PlatformException {
        // Desktop/tests keep using the C ABI fallback below.
      }
    }
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

  Future<Map<String, Object?>> getGameOverrides(String gameDir) async {
    try {
      final values = await _platformChannel.invokeMapMethod<String, Object?>('getGameOverrides', {'gameDir': gameDir});
      return values ?? const {};
    } on MissingPluginException {
      return const {};
    }
  }

  Future<void> updateGameOverride(String gameDir, String key, Object? value) async {
    try {
      await _platformChannel.invokeMethod<void>('updateGameOverride', {'gameDir': gameDir, 'key': key, 'value': value});
    } on MissingPluginException {
      return;
    }
  }

  Future<void> clearGameOverrides(String gameDir) async {
    try {
      await _platformChannel.invokeMethod<void>('clearGameOverrides', {'gameDir': gameDir});
    } on MissingPluginException {
      return;
    }
  }

  Future<void> openSettings() async {
    await _platformChannel.invokeMethod<void>('openSettings');
  }

  Future<void> openDiagnostics() async {
    await _platformChannel.invokeMethod<void>('openDiagnostics');
  }

  Future<Map<String, Object?>> getDiagnosticsInfo() async {
    try {
      final info = await _platformChannel.invokeMapMethod<String, Object?>('getDiagnosticsInfo');
      if (info != null) {
        return info;
      }
    } on MissingPluginException {
      // Non-Android platforms report the local Flutter-side state below.
    }
    return {
      'platform': Platform.operatingSystem,
      'platformVersion': Platform.operatingSystemVersion,
      'fileManagementGranted': !Platform.isAndroid,
      'gameRoot': await getGameRoot(),
      'scanDepth': await getScanDepth(),
      'logDir': '',
      'latestLog': '',
      'fileLogEnabled': false,
      'nativeLogConfigured': false,
    };
  }

  Future<Map<String, Object?>> getLauncherSettings() async {
    try {
      final settings = await _platformChannel.invokeMapMethod<String, Object?>('getLauncherSettings');
      if (settings != null) {
        return settings;
      }
    } on MissingPluginException {
      // Desktop fallback below.
    }
    return {
      'language': 'en',
      'forceLandscape': false,
      'useFfmpegImageDecoder': false,
      'ffmpegDecodeMode': 'software',
      'fileLogEnabled': false,
      'fileLogAutoCleanup': false,
      'fileLogRetentionDays': 15,
      'scanDepth': await getScanDepth(),
    };
  }

  Future<void> updateLauncherSetting(String key, Object? value) async {
    await _platformChannel.invokeMethod<void>('updateLauncherSetting', {'key': key, 'value': value});
  }

  Future<Map<String, Object?>> getEngineSettings() async {
    try {
      final settings = await _platformChannel.invokeMapMethod<String, Object?>('getEngineSettings');
      return settings ?? const {};
    } on MissingPluginException {
      return const {
        'renderer': 'software',
        'graphics_backend': 'opengl',
        'fps_limit': '60',
        'showfps': false,
        'ogl_accurate_render': false,
        'ffmpeg_image_decoder': false,
        'ffmpeg_decode_mode': 'software',
        'software_draw_thread': '0',
        'software_compress_tex': 'none',
        'ogl_max_texsize': '0',
        'ogl_compress_tex': 'none',
      };
    }
  }

  Future<void> updateEngineSetting(String key, Object? value) async {
    try {
      await _platformChannel.invokeMethod<void>('updateEngineSetting', {'key': key, 'value': value});
    } on MissingPluginException {
      return;
    }
  }

  Future<void> resetEngineSettings() async {
    try {
      await _platformChannel.invokeMethod<void>('resetEngineSettings');
    } on MissingPluginException {
      return;
    }
  }

  Future<void> launchOriginalEngine() async {
    await _platformChannel.invokeMethod<void>('launchOriginalEngine');
  }

  Future<int> performOverlayAction(String actionName) async {
    if (!Platform.isAndroid && !Platform.isIOS && !Platform.isMacOS && !Platform.isLinux && !Platform.isWindows) {
      return 0;
    }
    final pointer = actionName.toNativeUtf8();
    try {
      final result = _performOverlayAction(pointer);
      if (result < 0) {
        throw StateError('KR2LauncherPerformOverlayAction failed: $result');
      }
      return result;
    } on ArgumentError {
      return 0;
    } finally {
      calloc.free(pointer);
    }
  }

  Future<List<GameMenuItem>> getMainMenu() async {
    Pointer<Utf8> pointer;
    try {
      pointer = _getMainMenuJson();
    } on ArgumentError {
      return const [];
    }
    if (pointer == nullptr) {
      return const [];
    }
    final decoded = jsonDecode(pointer.toDartString());
    if (decoded is! List) {
      return const [];
    }
    return decoded.whereType<Map>().map((item) => GameMenuItem.fromMap(item.cast<String, Object?>())).toList(growable: false);
  }

  Future<void> activateMenuItem(GameMenuItem item) async {
    final path = item.path.toNativeUtf8();
    try {
      final result = _activateMenuItem(path);
      if (result != 0) {
        throw StateError('KR2LauncherActivateMenuItem failed: $result');
      }
    } on ArgumentError {
      return;
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
