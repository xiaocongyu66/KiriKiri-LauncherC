import 'dart:io';

import 'package:flutter/material.dart';

import '../bridge/launcher_bridge.dart';
import '../models/game_entry.dart';
import '../widgets/resource_icon.dart';

class LauncherHomePage extends StatefulWidget {
  const LauncherHomePage({required this.bridge, super.key});

  final LauncherBridge bridge;

  @override
  State<LauncherHomePage> createState() => _LauncherHomePageState();
}

class _LauncherHomePageState extends State<LauncherHomePage> with WidgetsBindingObserver {
  final TextEditingController _rootController = TextEditingController(text: '/storage/emulated/0/krkr2pro');

  int _pageIndex = 0;
  bool _loading = true;
  bool _storageGranted = false;
  String _scanStatus = '准备扫描';
  List<GameEntry> _games = const [];
  GameEntry? _selectedGame;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _loadInitialState();
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _rootController.dispose();
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      _refreshPermission();
    }
  }

  Future<void> _loadInitialState() async {
    await _refreshPermission();
    try {
      _rootController.text = await widget.bridge.getGameRoot();
    } catch (_) {}
    await _scanGames();
  }

  Future<void> _refreshPermission() async {
    try {
      final granted = await widget.bridge.hasFileManagementPermission();
      if (mounted) {
        setState(() => _storageGranted = granted);
      }
    } catch (_) {
      if (mounted) {
        setState(() => _storageGranted = !Platform.isAndroid);
      }
    }
  }

  String get _rootPath {
    final value = _rootController.text.trim();
    return value.isEmpty ? '/storage/emulated/0/krkr2pro' : value;
  }

  Future<void> _requestPermission() async {
    try {
      await widget.bridge.requestFileManagementPermission();
    } catch (_) {
      _showSnack('当前平台没有独立文件管理权限入口');
    }
    await _refreshPermission();
  }

  Future<void> _pickRoot() async {
    try {
      await widget.bridge.pickGame();
    } catch (_) {
      _showSnack('目录选择暂未接入，请直接填写路径');
    }
  }

  Future<void> _scanGames() async {
    final root = _rootPath;
    setState(() {
      _loading = true;
      _scanStatus = '扫描中';
    });
    try {
      await widget.bridge.setGameRoot(root);
      final depth = await widget.bridge.getScanDepth();
      final games = await widget.bridge.scanGames(rootPath: root, maxDepth: depth);
      if (!mounted) {
        return;
      }
      setState(() {
        _games = games;
        _selectedGame = games.isEmpty ? null : games.firstWhere((game) => game.path == _selectedGame?.path, orElse: () => games.first);
        _scanStatus = games.isEmpty ? '未找到游戏' : '已找到 ${games.length} 个游戏';
      });
    } catch (_) {
      if (!mounted) {
        return;
      }
      setState(() {
        _games = const [];
        _selectedGame = null;
        _scanStatus = '扫描失败';
      });
    } finally {
      if (mounted) {
        setState(() => _loading = false);
      }
    }
  }

  Future<void> _launch(GameEntry game) async {
    try {
      await widget.bridge.launchGame(game);
    } catch (error) {
      _showSnack('启动失败：$error');
    }
  }

  void _setSelected(GameEntry game) {
    setState(() => _selectedGame = game);
  }

  void _updateSelected(GameEntry game) {
    setState(() {
      _selectedGame = game;
      _games = _games.map((entry) => entry.path == game.path ? game : entry).toList(growable: false);
    });
  }

  void _showSnack(String message) {
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
    }
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final wide = constraints.maxWidth >= 840;
        final content = switch (_pageIndex) {
          0 => _GamesPage(
              wide: wide,
              loading: _loading,
              storageGranted: _storageGranted,
              scanStatus: _scanStatus,
              rootController: _rootController,
              games: _games,
              selectedGame: _selectedGame,
              onRequestPermission: _requestPermission,
              onPickRoot: _pickRoot,
              onScan: _scanGames,
              onSelect: _setSelected,
              onUpdate: _updateSelected,
              onLaunch: _launch,
            ),
          1 => _SettingsPage(
              bridge: widget.bridge,
              storageGranted: _storageGranted,
              rootController: _rootController,
              onRequestPermission: _requestPermission,
              onPickRoot: _pickRoot,
              onScan: _scanGames,
              onOpenSystemSettings: () => widget.bridge.openSettings(),
            ),
          _ => _DiagnosticsPage(bridge: widget.bridge, storageGranted: _storageGranted, rootPath: _rootPath, scanStatus: _scanStatus),
        };

        return Scaffold(
          appBar: AppBar(
            title: const Text('KiriKiri Launcher'),
            actions: [
              IconButton(onPressed: _scanGames, tooltip: '扫描', icon: const Icon(Icons.refresh_rounded)),
              IconButton(onPressed: _requestPermission, tooltip: '文件权限', icon: const Icon(Icons.folder_special_outlined)),
            ],
          ),
          body: Row(
            children: [
              if (wide)
                NavigationRail(
                  selectedIndex: _pageIndex,
                  onDestinationSelected: (index) => setState(() => _pageIndex = index),
                  labelType: NavigationRailLabelType.all,
                  leading: const Padding(
                    padding: EdgeInsets.only(bottom: 12),
                    child: ResourceIcon('menu_icon.png', size: 28),
                  ),
                  destinations: const [
                    NavigationRailDestination(icon: Icon(Icons.sports_esports_outlined), selectedIcon: Icon(Icons.sports_esports), label: Text('游戏')),
                    NavigationRailDestination(icon: Icon(Icons.tune_outlined), selectedIcon: Icon(Icons.tune), label: Text('设置')),
                    NavigationRailDestination(icon: Icon(Icons.bug_report_outlined), selectedIcon: Icon(Icons.bug_report), label: Text('诊断')),
                  ],
                ),
              Expanded(child: SafeArea(child: content)),
            ],
          ),
          bottomNavigationBar: wide
              ? null
              : NavigationBar(
                  selectedIndex: _pageIndex,
                  onDestinationSelected: (index) => setState(() => _pageIndex = index),
                  destinations: const [
                    NavigationDestination(icon: Icon(Icons.sports_esports_outlined), selectedIcon: Icon(Icons.sports_esports), label: '游戏'),
                    NavigationDestination(icon: Icon(Icons.tune_outlined), selectedIcon: Icon(Icons.tune), label: '设置'),
                    NavigationDestination(icon: Icon(Icons.bug_report_outlined), selectedIcon: Icon(Icons.bug_report), label: '诊断'),
                  ],
                ),
        );
      },
    );
  }
}

class _GamesPage extends StatelessWidget {
  const _GamesPage({
    required this.wide,
    required this.loading,
    required this.storageGranted,
    required this.scanStatus,
    required this.rootController,
    required this.games,
    required this.selectedGame,
    required this.onRequestPermission,
    required this.onPickRoot,
    required this.onScan,
    required this.onSelect,
    required this.onUpdate,
    required this.onLaunch,
  });

  final bool wide;
  final bool loading;
  final bool storageGranted;
  final String scanStatus;
  final TextEditingController rootController;
  final List<GameEntry> games;
  final GameEntry? selectedGame;
  final VoidCallback onRequestPermission;
  final VoidCallback onPickRoot;
  final VoidCallback onScan;
  final ValueChanged<GameEntry> onSelect;
  final ValueChanged<GameEntry> onUpdate;
  final ValueChanged<GameEntry> onLaunch;

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _StatusPanel(
          storageGranted: storageGranted,
          scanStatus: scanStatus,
          gameCount: games.length,
          onRequestPermission: onRequestPermission,
          onScan: onScan,
        ),
        const SizedBox(height: 12),
        _RootPanel(controller: rootController, onPickRoot: onPickRoot, onScan: onScan),
        const SizedBox(height: 12),
        if (wide)
          SizedBox(
            height: 560,
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Expanded(flex: 5, child: _GameList(loading: loading, games: games, selectedGame: selectedGame, onSelect: onSelect)),
                const SizedBox(width: 12),
                Expanded(flex: 4, child: _GameDetail(game: selectedGame, onUpdate: onUpdate, onLaunch: onLaunch)),
              ],
            ),
          )
        else ...[
          _GameList(loading: loading, games: games, selectedGame: selectedGame, onSelect: onSelect),
          const SizedBox(height: 12),
          _GameDetail(game: selectedGame, onUpdate: onUpdate, onLaunch: onLaunch),
        ],
      ],
    );
  }
}

class _StatusPanel extends StatelessWidget {
  const _StatusPanel({
    required this.storageGranted,
    required this.scanStatus,
    required this.gameCount,
    required this.onRequestPermission,
    required this.onScan,
  });

  final bool storageGranted;
  final String scanStatus;
  final int gameCount;
  final VoidCallback onRequestPermission;
  final VoidCallback onScan;

  @override
  Widget build(BuildContext context) {
    return _Card(
      child: Wrap(
        spacing: 12,
        runSpacing: 12,
        crossAxisAlignment: WrapCrossAlignment.center,
        children: [
          _Metric(icon: Icons.folder_special_outlined, title: storageGranted ? '文件权限已授权' : '需要文件管理权限', subtitle: storageGranted ? '可扫描外部存储游戏' : 'Android 11+ 必须授权后才能读取游戏'),
          _Metric(icon: Icons.search_rounded, title: scanStatus, subtitle: '游戏数量：$gameCount'),
          FilledButton.icon(onPressed: onScan, icon: const Icon(Icons.refresh_rounded), label: const Text('扫描')),
          if (!storageGranted) OutlinedButton.icon(onPressed: onRequestPermission, icon: const Icon(Icons.admin_panel_settings_outlined), label: const Text('授权')),
        ],
      ),
    );
  }
}

class _RootPanel extends StatelessWidget {
  const _RootPanel({required this.controller, required this.onPickRoot, required this.onScan});

  final TextEditingController controller;
  final VoidCallback onPickRoot;
  final VoidCallback onScan;

  @override
  Widget build(BuildContext context) {
    return _Card(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const _SectionTitle(icon: Icons.folder_open_outlined, title: '游戏目录'),
          const SizedBox(height: 12),
          TextField(
            controller: controller,
            decoration: const InputDecoration(hintText: '/storage/emulated/0/krkr2pro', labelText: '扫描根目录'),
            onSubmitted: (_) => onScan(),
          ),
          const SizedBox(height: 10),
          Wrap(
            spacing: 10,
            children: [
              OutlinedButton.icon(onPressed: onPickRoot, icon: const Icon(Icons.drive_folder_upload_outlined), label: const Text('选择目录')),
              FilledButton.icon(onPressed: onScan, icon: const Icon(Icons.saved_search), label: const Text('保存并扫描')),
            ],
          ),
        ],
      ),
    );
  }
}

class _GameList extends StatelessWidget {
  const _GameList({required this.loading, required this.games, required this.selectedGame, required this.onSelect});

  final bool loading;
  final List<GameEntry> games;
  final GameEntry? selectedGame;
  final ValueChanged<GameEntry> onSelect;

  @override
  Widget build(BuildContext context) {
    return _Card(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const _SectionTitle(icon: Icons.list_alt_rounded, title: '游戏列表'),
          const SizedBox(height: 8),
          if (loading) const LinearProgressIndicator(),
          if (!loading && games.isEmpty) const _EmptyState(message: '没有找到游戏。请检查权限、目录和扫描深度。'),
          if (games.isNotEmpty)
            SizedBox(
              height: (games.length * 72.0).clamp(120.0, 420.0).toDouble(),
              child: ListView.separated(
                itemCount: games.length,
                separatorBuilder: (_, __) => const Divider(height: 1),
                itemBuilder: (context, index) {
                  final game = games[index];
                  final selected = selectedGame?.path == game.path;
                  return ListTile(
                    selected: selected,
                    leading: _Cover(path: game.coverPath, size: 44),
                    title: Text(game.title, maxLines: 1, overflow: TextOverflow.ellipsis),
                    subtitle: Text(game.path, maxLines: 1, overflow: TextOverflow.ellipsis),
                    trailing: const Icon(Icons.chevron_right_rounded),
                    onTap: () => onSelect(game),
                  );
                },
              ),
            ),
        ],
      ),
    );
  }
}

class _GameDetail extends StatelessWidget {
  const _GameDetail({required this.game, required this.onUpdate, required this.onLaunch});

  final GameEntry? game;
  final ValueChanged<GameEntry> onUpdate;
  final ValueChanged<GameEntry> onLaunch;

  @override
  Widget build(BuildContext context) {
    final item = game;
    return _Card(
      child: item == null
          ? const _EmptyState(message: '请选择一个游戏')
          : Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    _Cover(path: item.coverPath, size: 64),
                    const SizedBox(width: 12),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(item.title, style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w700)),
                          const SizedBox(height: 4),
                          Text(item.path, maxLines: 2, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.bodySmall),
                        ],
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                _LaunchFilePicker(game: item, onChanged: onUpdate),
                const SizedBox(height: 16),
                FilledButton.icon(
                  onPressed: () => onLaunch(item),
                  icon: const Icon(Icons.play_arrow_rounded),
                  label: const Text('启动游戏'),
                ),
              ],
            ),
    );
  }
}

class _SettingsPage extends StatefulWidget {
  const _SettingsPage({
    required this.bridge,
    required this.storageGranted,
    required this.rootController,
    required this.onRequestPermission,
    required this.onPickRoot,
    required this.onScan,
    required this.onOpenSystemSettings,
  });

  final LauncherBridge bridge;
  final bool storageGranted;
  final TextEditingController rootController;
  final VoidCallback onRequestPermission;
  final VoidCallback onPickRoot;
  final VoidCallback onScan;
  final Future<void> Function() onOpenSystemSettings;

  @override
  State<_SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<_SettingsPage> {
  late Future<Map<String, Object?>> _future = widget.bridge.getLauncherSettings();
  Map<String, Object?> _settings = const {};

  Future<void> _reload() async {
    final settings = await widget.bridge.getLauncherSettings();
    if (mounted) {
      setState(() {
        _settings = settings;
        _future = Future.value(settings);
      });
    }
  }

  Future<void> _set(String key, Object? value) async {
    setState(() => _settings = {..._settings, key: value});
    try {
      await widget.bridge.updateLauncherSetting(key, value);
    } catch (_) {}
    await _reload();
  }

  bool _bool(String key, bool fallback) => _settings[key] is bool ? _settings[key] as bool : fallback;

  int _int(String key, int fallback) => _settings[key] is int ? _settings[key] as int : fallback;

  String _string(String key, String fallback) => _settings[key] is String ? _settings[key] as String : fallback;

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<Map<String, Object?>>(
      future: _future,
      builder: (context, snapshot) {
        if (snapshot.hasData && !identical(_settings, snapshot.data)) {
          _settings = snapshot.data!;
        }
        final language = _string('language', 'en');
        final decodeMode = _string('ffmpegDecodeMode', 'software');
        final retentionDays = _int('fileLogRetentionDays', 15).clamp(1, 60).toInt();
        final scanDepth = _int('scanDepth', 2).clamp(1, 10).toInt();
        return ListView(
          padding: const EdgeInsets.all(16),
          children: [
            _Card(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _SectionTitle(icon: Icons.security_outlined, title: '权限', trailing: IconButton(onPressed: _reload, icon: const Icon(Icons.refresh_rounded))),
                  _ActionRow(
                    icon: Icons.folder_special_outlined,
                    title: widget.storageGranted ? '文件管理权限已授权' : '申请文件管理权限',
                    subtitle: '沿用旧版启动器逻辑，Android 11+ 打开所有文件访问权限页。',
                    onTap: widget.onRequestPermission,
                  ),
                  _ActionRow(
                    icon: Icons.settings_applications_outlined,
                    title: '系统应用设置',
                    subtitle: '打开 Android 应用详情，处理通知、存储等系统权限。',
                    onTap: () => widget.onOpenSystemSettings(),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 12),
            _Card(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const _SectionTitle(icon: Icons.folder_open_outlined, title: '游戏扫描'),
                  const SizedBox(height: 12),
                  TextField(controller: widget.rootController, decoration: const InputDecoration(labelText: '游戏根目录')),
                  const SizedBox(height: 10),
                  Wrap(
                    spacing: 10,
                    children: [
                      OutlinedButton.icon(onPressed: widget.onPickRoot, icon: const Icon(Icons.drive_folder_upload_outlined), label: const Text('选择目录')),
                      FilledButton.icon(onPressed: widget.onScan, icon: const Icon(Icons.saved_search), label: const Text('保存并扫描')),
                    ],
                  ),
                  const SizedBox(height: 12),
                  _SliderSetting(
                    title: '扫描深度',
                    value: scanDepth,
                    min: 1,
                    max: 10,
                    onChanged: (value) => _set('scanDepth', value),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 12),
            _Card(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const _SectionTitle(icon: Icons.display_settings_outlined, title: '显示'),
                  _ChoiceSetting(
                    title: '语言',
                    value: language,
                    choices: const {'en': 'English', 'zh': '中文'},
                    onChanged: (value) => _set('language', value),
                  ),
                  _SwitchSetting(
                    title: '强制横屏',
                    subtitle: '旧版默认开启，启动器和引擎界面优先横屏。',
                    value: _bool('forceLandscape', true),
                    onChanged: (value) => _set('forceLandscape', value),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 12),
            _Card(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const _SectionTitle(icon: Icons.tune_outlined, title: '引擎'),
                  _ChoiceSetting(
                    title: 'FFmpeg 解码模式',
                    value: decodeMode,
                    choices: const {'software': '软件', 'hardware': '硬件'},
                    onChanged: (value) => _set('ffmpegDecodeMode', value),
                  ),
                  _SwitchSetting(
                    title: 'FFmpeg 图片解码器',
                    subtitle: '同步写入旧引擎偏好，启动游戏前由 MainActivity 应用。',
                    value: _bool('useFfmpegImageDecoder', false),
                    onChanged: (value) => _set('useFfmpegImageDecoder', value),
                  ),
                  _ActionRow(
                    icon: Icons.open_in_new_rounded,
                    title: '打开原始引擎',
                    subtitle: '不选择游戏，直接进入旧版引擎入口。',
                    onTap: () => widget.bridge.launchOriginalEngine(),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 12),
            _Card(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const _SectionTitle(icon: Icons.article_outlined, title: '日志'),
                  _SwitchSetting(
                    title: '文件日志',
                    subtitle: '/storage/emulated/0/krkr2pro/logs',
                    value: _bool('fileLogEnabled', true),
                    onChanged: (value) => _set('fileLogEnabled', value),
                  ),
                  _SwitchSetting(
                    title: '自动清理旧日志',
                    subtitle: '沿用旧版保留策略。',
                    value: _bool('fileLogAutoCleanup', true),
                    onChanged: (value) => _set('fileLogAutoCleanup', value),
                  ),
                  _SliderSetting(
                    title: '日志保留天数',
                    value: retentionDays,
                    min: 1,
                    max: 60,
                    onChanged: (value) => _set('fileLogRetentionDays', value),
                  ),
                ],
              ),
            ),
          ],
        );
      },
    );
  }
}

class _DiagnosticsPage extends StatefulWidget {
  const _DiagnosticsPage({required this.bridge, required this.storageGranted, required this.rootPath, required this.scanStatus});

  final LauncherBridge bridge;
  final bool storageGranted;
  final String rootPath;
  final String scanStatus;

  @override
  State<_DiagnosticsPage> createState() => _DiagnosticsPageState();
}

class _DiagnosticsPageState extends State<_DiagnosticsPage> {
  late Future<Map<String, Object?>> _future = widget.bridge.getDiagnosticsInfo();

  void _reload() {
    setState(() => _future = widget.bridge.getDiagnosticsInfo());
  }

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<Map<String, Object?>>(
      future: _future,
      builder: (context, snapshot) {
        final info = snapshot.data ?? const <String, Object?>{};
        return ListView(
          padding: const EdgeInsets.all(16),
          children: [
            _Card(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _SectionTitle(icon: Icons.bug_report_outlined, title: '诊断', trailing: IconButton(onPressed: _reload, icon: const Icon(Icons.refresh_rounded))),
                  const SizedBox(height: 8),
                  if (snapshot.connectionState == ConnectionState.waiting) const LinearProgressIndicator(),
                  _InfoLine('平台', '${info['platform'] ?? Platform.operatingSystem} ${info['platformVersion'] ?? ''}'.trim()),
                  _InfoLine('设备', '${info['device'] ?? '-'}'),
                  _InfoLine('包名', '${info['packageName'] ?? '-'}'),
                  _InfoLine('文件权限', '${info['fileManagementGranted'] ?? widget.storageGranted}'),
                  _InfoLine('扫描状态', widget.scanStatus),
                  _InfoLine('游戏目录', '${info['gameRoot'] ?? widget.rootPath}'),
                  _InfoLine('扫描深度', '${info['scanDepth'] ?? '-'}'),
                ],
              ),
            ),
            const SizedBox(height: 12),
            _Card(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const _SectionTitle(icon: Icons.article_outlined, title: '日志'),
                  const SizedBox(height: 8),
                  _InfoLine('日志目录', '${info['logDir'] ?? '-'}'),
                  _InfoLine('最新日志', '${info['latestLog'] ?? '-'}'),
                  _InfoLine('文件日志', '${info['fileLogEnabled'] ?? false}'),
                  _InfoLine('Native 日志', '${info['nativeLogConfigured'] ?? false}'),
                ],
              ),
            ),
          ],
        );
      },
    );
  }
}

class _LaunchFilePicker extends StatefulWidget {
  const _LaunchFilePicker({required this.game, required this.onChanged});

  final GameEntry game;
  final ValueChanged<GameEntry> onChanged;

  @override
  State<_LaunchFilePicker> createState() => _LaunchFilePickerState();
}

class _LaunchFilePickerState extends State<_LaunchFilePicker> {
  late Future<List<String>> _future;
  String _selected = '';

  @override
  void initState() {
    super.initState();
    _selected = widget.game.launchFile ?? '';
    _future = LauncherBridge.instance.listLaunchCandidates(widget.game.path);
  }

  @override
  void didUpdateWidget(covariant _LaunchFilePicker oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.game.path != widget.game.path) {
      _selected = widget.game.launchFile ?? '';
      _future = LauncherBridge.instance.listLaunchCandidates(widget.game.path);
    }
  }

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<List<String>>(
      future: _future,
      builder: (context, snapshot) {
        final candidates = snapshot.data ?? const <String>[];
        final values = ['', ...candidates];
        return DropdownButtonFormField<String>(
          value: values.contains(_selected) ? _selected : '',
          isExpanded: true,
          decoration: const InputDecoration(labelText: '启动文件'),
          items: values.map((path) => DropdownMenuItem(value: path, child: Text(path.isEmpty ? '自动检测' : _relativeToGame(path, widget.game.path), overflow: TextOverflow.ellipsis))).toList(growable: false),
          onChanged: (path) {
            final next = path ?? '';
            setState(() => _selected = next);
            widget.onChanged(widget.game.copyWith(launchFile: next));
          },
        );
      },
    );
  }
}

class _Card extends StatelessWidget {
  const _Card({required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Card(child: Padding(padding: const EdgeInsets.all(16), child: child));
  }
}

class _SectionTitle extends StatelessWidget {
  const _SectionTitle({required this.icon, required this.title, this.trailing});

  final IconData icon;
  final String title;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 20, color: Theme.of(context).colorScheme.primary),
        const SizedBox(width: 8),
        Expanded(child: Text(title, style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700))),
        if (trailing != null) trailing!,
      ],
    );
  }
}

class _Metric extends StatelessWidget {
  const _Metric({required this.icon, required this.title, required this.subtitle});

  final IconData icon;
  final String title;
  final String subtitle;

  @override
  Widget build(BuildContext context) {
    return ConstrainedBox(
      constraints: const BoxConstraints(minWidth: 220, maxWidth: 360),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, color: Theme.of(context).colorScheme.primary),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(title, maxLines: 1, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.w700)),
                Text(subtitle, maxLines: 1, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.bodySmall),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _ActionRow extends StatelessWidget {
  const _ActionRow({required this.icon, required this.title, required this.subtitle, required this.onTap});

  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return ListTile(
      contentPadding: EdgeInsets.zero,
      leading: Icon(icon),
      title: Text(title),
      subtitle: Text(subtitle),
      trailing: const Icon(Icons.chevron_right_rounded),
      onTap: onTap,
    );
  }
}

class _SwitchSetting extends StatelessWidget {
  const _SwitchSetting({required this.title, required this.subtitle, required this.value, required this.onChanged});

  final String title;
  final String subtitle;
  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    return SwitchListTile(
      contentPadding: EdgeInsets.zero,
      title: Text(title),
      subtitle: Text(subtitle),
      value: value,
      onChanged: onChanged,
    );
  }
}

class _ChoiceSetting extends StatelessWidget {
  const _ChoiceSetting({required this.title, required this.value, required this.choices, required this.onChanged});

  final String title;
  final String value;
  final Map<String, String> choices;
  final ValueChanged<String> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(title, style: Theme.of(context).textTheme.titleSmall),
          const SizedBox(height: 8),
          Wrap(
            spacing: 8,
            children: choices.entries
                .map(
                  (entry) => ChoiceChip(
                    label: Text(entry.value),
                    selected: value == entry.key,
                    onSelected: (_) => onChanged(entry.key),
                  ),
                )
                .toList(growable: false),
          ),
        ],
      ),
    );
  }
}

class _SliderSetting extends StatelessWidget {
  const _SliderSetting({required this.title, required this.value, required this.min, required this.max, required this.onChanged});

  final String title;
  final int value;
  final int min;
  final int max;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('$title：$value', style: Theme.of(context).textTheme.titleSmall),
          Slider(
            value: value.toDouble(),
            min: min.toDouble(),
            max: max.toDouble(),
            divisions: max - min,
            label: '$value',
            onChanged: (next) => onChanged(next.round().clamp(min, max).toInt()),
          ),
        ],
      ),
    );
  }
}

class _InfoLine extends StatelessWidget {
  const _InfoLine(this.label, this.value);

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(width: 88, child: Text(label, style: Theme.of(context).textTheme.bodySmall?.copyWith(color: Theme.of(context).colorScheme.onSurfaceVariant))),
          Expanded(child: SelectableText(value.isEmpty ? '-' : value)),
        ],
      ),
    );
  }
}

class _EmptyState extends StatelessWidget {
  const _EmptyState({required this.message});

  final String message;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 36),
      child: Center(child: Text(message, textAlign: TextAlign.center, style: Theme.of(context).textTheme.bodyMedium)),
    );
  }
}

class _Cover extends StatelessWidget {
  const _Cover({required this.path, required this.size});

  final String? path;
  final double size;

  @override
  Widget build(BuildContext context) {
    final image = _fileImage(path);
    return ClipRRect(
      borderRadius: BorderRadius.circular(12),
      child: Container(
        width: size,
        height: size,
        color: Theme.of(context).colorScheme.surfaceContainerHighest,
        child: image == null ? Icon(Icons.videogame_asset_outlined, size: size * 0.45) : Image(image: image, fit: BoxFit.cover),
      ),
    );
  }
}

ImageProvider? _fileImage(String? path) {
  if (path == null || path.isEmpty) {
    return null;
  }
  final file = File(path);
  if (!file.existsSync()) {
    return null;
  }
  return FileImage(file);
}

String _relativeToGame(String path, String gameDir) {
  final normalizedPath = path.replaceAll('\\', '/');
  final normalizedGameDir = gameDir.replaceAll('\\', '/');
  if (normalizedPath.startsWith('$normalizedGameDir/')) {
    return normalizedPath.substring(normalizedGameDir.length + 1);
  }
  return normalizedPath.split('/').last;
}
