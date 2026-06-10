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

  void _selectGame(GameEntry game) {
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
        final page = switch (_pageIndex) {
          0 => _LibraryPage(
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
              onSelect: _selectGame,
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
            titleSpacing: 16,
            title: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                const ResourceIcon('menu_icon.png', size: 24),
                const SizedBox(width: 8),
                const Text('KiriKiri'),
                const SizedBox(width: 10),
                _StatusDot(active: _storageGranted),
              ],
            ),
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
                  destinations: const [
                    NavigationRailDestination(icon: Icon(Icons.grid_view_outlined), selectedIcon: Icon(Icons.grid_view_rounded), label: Text('库')),
                    NavigationRailDestination(icon: Icon(Icons.tune_outlined), selectedIcon: Icon(Icons.tune_rounded), label: Text('设置')),
                    NavigationRailDestination(icon: Icon(Icons.monitor_heart_outlined), selectedIcon: Icon(Icons.monitor_heart_rounded), label: Text('诊断')),
                  ],
                ),
              Expanded(child: SafeArea(child: page)),
            ],
          ),
          bottomNavigationBar: wide
              ? null
              : NavigationBar(
                  selectedIndex: _pageIndex,
                  onDestinationSelected: (index) => setState(() => _pageIndex = index),
                  destinations: const [
                    NavigationDestination(icon: Icon(Icons.grid_view_outlined), selectedIcon: Icon(Icons.grid_view_rounded), label: '库'),
                    NavigationDestination(icon: Icon(Icons.tune_outlined), selectedIcon: Icon(Icons.tune_rounded), label: '设置'),
                    NavigationDestination(icon: Icon(Icons.monitor_heart_outlined), selectedIcon: Icon(Icons.monitor_heart_rounded), label: '诊断'),
                  ],
                ),
        );
      },
    );
  }
}

class _LibraryPage extends StatelessWidget {
  const _LibraryPage({
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
    final body = wide
        ? Row(
            children: [
              SizedBox(
                width: 420,
                child: _GameListPane(
                  loading: loading,
                  games: games,
                  selectedGame: selectedGame,
                  onSelect: onSelect,
                ),
              ),
              const VerticalDivider(width: 1),
              Expanded(child: _GameDetailPane(game: selectedGame, onUpdate: onUpdate, onLaunch: onLaunch)),
            ],
          )
        : ListView(
            padding: const EdgeInsets.all(12),
            children: [
              _GameListPane(loading: loading, games: games, selectedGame: selectedGame, onSelect: onSelect, shrinkWrap: true),
              const SizedBox(height: 12),
              _GameDetailPane(game: selectedGame, onUpdate: onUpdate, onLaunch: onLaunch),
            ],
          );

    return Column(
      children: [
        _CommandStrip(
          storageGranted: storageGranted,
          scanStatus: scanStatus,
          gameCount: games.length,
          rootController: rootController,
          onRequestPermission: onRequestPermission,
          onPickRoot: onPickRoot,
          onScan: onScan,
        ),
        const Divider(height: 1),
        Expanded(child: body),
      ],
    );
  }
}

class _CommandStrip extends StatelessWidget {
  const _CommandStrip({
    required this.storageGranted,
    required this.scanStatus,
    required this.gameCount,
    required this.rootController,
    required this.onRequestPermission,
    required this.onPickRoot,
    required this.onScan,
  });

  final bool storageGranted;
  final String scanStatus;
  final int gameCount;
  final TextEditingController rootController;
  final VoidCallback onRequestPermission;
  final VoidCallback onPickRoot;
  final VoidCallback onScan;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Material(
      color: scheme.surfaceContainerLowest,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 8, 12, 8),
        child: LayoutBuilder(
          builder: (context, constraints) {
            final compact = constraints.maxWidth < 720;
            final pathField = TextField(
              controller: rootController,
              decoration: const InputDecoration(prefixIcon: Icon(Icons.folder_open_rounded), labelText: '游戏目录', isDense: true),
              onSubmitted: (_) => onScan(),
            );
            final actions = Wrap(
              spacing: 8,
              runSpacing: 8,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                _MiniStatus(icon: storageGranted ? Icons.verified_rounded : Icons.warning_amber_rounded, label: storageGranted ? '已授权' : '未授权'),
                _MiniStatus(icon: Icons.sports_esports_rounded, label: '$gameCount'),
                _MiniStatus(icon: Icons.search_rounded, label: scanStatus),
                IconButton.filledTonal(onPressed: onPickRoot, tooltip: '选择目录', icon: const Icon(Icons.drive_folder_upload_rounded)),
                IconButton.filled(onPressed: onScan, tooltip: '扫描', icon: const Icon(Icons.refresh_rounded)),
                if (!storageGranted) IconButton.outlined(onPressed: onRequestPermission, tooltip: '申请文件权限', icon: const Icon(Icons.admin_panel_settings_rounded)),
              ],
            );
            if (compact) {
              return Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  pathField,
                  const SizedBox(height: 8),
                  actions,
                ],
              );
            }
            return Row(
              children: [
                Expanded(child: pathField),
                const SizedBox(width: 12),
                actions,
              ],
            );
          },
        ),
      ),
    );
  }
}

class _GameListPane extends StatelessWidget {
  const _GameListPane({
    required this.loading,
    required this.games,
    required this.selectedGame,
    required this.onSelect,
    this.shrinkWrap = false,
  });

  final bool loading;
  final List<GameEntry> games;
  final GameEntry? selectedGame;
  final ValueChanged<GameEntry> onSelect;
  final bool shrinkWrap;

  @override
  Widget build(BuildContext context) {
    if (loading) {
      return const _Pane(child: Center(child: CircularProgressIndicator()));
    }
    if (games.isEmpty) {
      return const _Pane(child: _EmptyState(icon: Icons.search_off_rounded, message: '未找到游戏'));
    }
    return _Pane(
      padding: EdgeInsets.zero,
      child: ListView.separated(
        shrinkWrap: shrinkWrap,
        physics: shrinkWrap ? const NeverScrollableScrollPhysics() : null,
        padding: const EdgeInsets.symmetric(vertical: 8),
        itemCount: games.length,
        separatorBuilder: (_, __) => const Divider(height: 1),
        itemBuilder: (context, index) {
          final game = games[index];
          final selected = selectedGame?.path == game.path;
          return ListTile(
            dense: true,
            selected: selected,
            leading: _Cover(path: game.coverPath, size: 42),
            title: Text(game.title, maxLines: 1, overflow: TextOverflow.ellipsis),
            subtitle: Text(game.path, maxLines: 1, overflow: TextOverflow.ellipsis),
            trailing: selected ? const Icon(Icons.check_circle_rounded) : const Icon(Icons.chevron_right_rounded),
            onTap: () => onSelect(game),
          );
        },
      ),
    );
  }
}

class _GameDetailPane extends StatelessWidget {
  const _GameDetailPane({required this.game, required this.onUpdate, required this.onLaunch});

  final GameEntry? game;
  final ValueChanged<GameEntry> onUpdate;
  final ValueChanged<GameEntry> onLaunch;

  @override
  Widget build(BuildContext context) {
    final item = game;
    if (item == null) {
      return const _Pane(child: _EmptyState(icon: Icons.videogame_asset_outlined, message: '选择一个游戏'));
    }
    return _Pane(
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _Cover(path: item.coverPath, size: 76),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(item.title, maxLines: 2, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w700)),
                    const SizedBox(height: 6),
                    SelectableText(item.path, style: Theme.of(context).textTheme.bodySmall),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 16),
          _LaunchFilePicker(game: item, onChanged: onUpdate),
          const SizedBox(height: 12),
          FilledButton.icon(onPressed: () => onLaunch(item), icon: const Icon(Icons.play_arrow_rounded), label: const Text('启动')),
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
          padding: const EdgeInsets.all(12),
          children: [
            _SettingsGroup(
              title: '权限',
              icon: Icons.security_rounded,
              trailing: IconButton(onPressed: _reload, tooltip: '刷新', icon: const Icon(Icons.refresh_rounded)),
              children: [
                _ActionTile(icon: Icons.folder_special_rounded, title: widget.storageGranted ? '文件管理权限' : '申请文件管理权限', subtitle: widget.storageGranted ? '已授权' : '需要授权后扫描外部存储', onTap: widget.onRequestPermission),
                _ActionTile(icon: Icons.settings_applications_rounded, title: '系统应用设置', subtitle: '权限、存储、通知', onTap: () => widget.onOpenSystemSettings()),
              ],
            ),
            _SettingsGroup(
              title: '扫描',
              icon: Icons.saved_search_rounded,
              children: [
                Padding(
                  padding: const EdgeInsets.fromLTRB(12, 8, 12, 8),
                  child: TextField(controller: widget.rootController, decoration: const InputDecoration(labelText: '游戏根目录', isDense: true)),
                ),
                Padding(
                  padding: const EdgeInsets.fromLTRB(12, 0, 12, 8),
                  child: Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      OutlinedButton.icon(onPressed: widget.onPickRoot, icon: const Icon(Icons.drive_folder_upload_rounded), label: const Text('选择')),
                      FilledButton.icon(onPressed: widget.onScan, icon: const Icon(Icons.refresh_rounded), label: const Text('扫描')),
                    ],
                  ),
                ),
                _SliderTile(title: '扫描深度', value: scanDepth, min: 1, max: 10, onChanged: (value) => _set('scanDepth', value)),
              ],
            ),
            _SettingsGroup(
              title: '显示',
              icon: Icons.display_settings_rounded,
              children: [
                _ChoiceTile(title: '语言', value: language, choices: const {'en': 'English', 'zh': '中文'}, onChanged: (value) => _set('language', value)),
                _SwitchTile(title: '强制横屏', subtitle: '启动器和引擎优先横屏', value: _bool('forceLandscape', true), onChanged: (value) => _set('forceLandscape', value)),
              ],
            ),
            _SettingsGroup(
              title: '引擎',
              icon: Icons.tune_rounded,
              children: [
                _ChoiceTile(title: 'FFmpeg 解码', value: decodeMode, choices: const {'software': '软件', 'hardware': '硬件'}, onChanged: (value) => _set('ffmpegDecodeMode', value)),
                _SwitchTile(title: 'FFmpeg 图片解码', subtitle: '启动游戏前应用', value: _bool('useFfmpegImageDecoder', false), onChanged: (value) => _set('useFfmpegImageDecoder', value)),
                _ActionTile(icon: Icons.open_in_new_rounded, title: '打开原始引擎', subtitle: '无游戏参数启动', onTap: () => widget.bridge.launchOriginalEngine()),
              ],
            ),
            _SettingsGroup(
              title: '日志',
              icon: Icons.article_rounded,
              children: [
                _SwitchTile(title: '文件日志', subtitle: '/storage/emulated/0/krkr2pro/logs', value: _bool('fileLogEnabled', true), onChanged: (value) => _set('fileLogEnabled', value)),
                _SwitchTile(title: '自动清理', subtitle: '按保留天数删除旧日志', value: _bool('fileLogAutoCleanup', true), onChanged: (value) => _set('fileLogAutoCleanup', value)),
                _SliderTile(title: '保留天数', value: retentionDays, min: 1, max: 60, onChanged: (value) => _set('fileLogRetentionDays', value)),
              ],
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
          padding: const EdgeInsets.all(12),
          children: [
            _SettingsGroup(
              title: '运行状态',
              icon: Icons.monitor_heart_rounded,
              trailing: IconButton(onPressed: _reload, tooltip: '刷新', icon: const Icon(Icons.refresh_rounded)),
              children: [
                if (snapshot.connectionState == ConnectionState.waiting) const LinearProgressIndicator(),
                _InfoRow('平台', '${info['platform'] ?? Platform.operatingSystem} ${info['platformVersion'] ?? ''}'.trim()),
                _InfoRow('设备', '${info['device'] ?? '-'}'),
                _InfoRow('包名', '${info['packageName'] ?? '-'}'),
                _InfoRow('文件权限', '${info['fileManagementGranted'] ?? widget.storageGranted}'),
                _InfoRow('扫描状态', widget.scanStatus),
                _InfoRow('游戏目录', '${info['gameRoot'] ?? widget.rootPath}'),
                _InfoRow('扫描深度', '${info['scanDepth'] ?? '-'}'),
              ],
            ),
            _SettingsGroup(
              title: '日志',
              icon: Icons.article_rounded,
              children: [
                _InfoRow('目录', '${info['logDir'] ?? '-'}'),
                _InfoRow('最新', '${info['latestLog'] ?? '-'}'),
                _InfoRow('文件日志', '${info['fileLogEnabled'] ?? false}'),
                _InfoRow('Native', '${info['nativeLogConfigured'] ?? false}'),
              ],
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
          decoration: const InputDecoration(labelText: '启动文件', isDense: true),
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

class _Pane extends StatelessWidget {
  const _Pane({required this.child, this.padding = const EdgeInsets.all(12)});

  final Widget child;
  final EdgeInsetsGeometry padding;

  @override
  Widget build(BuildContext context) {
    return Material(color: Theme.of(context).colorScheme.surfaceContainerLowest, child: Padding(padding: padding, child: child));
  }
}

class _SettingsGroup extends StatelessWidget {
  const _SettingsGroup({required this.title, required this.icon, required this.children, this.trailing});

  final String title;
  final IconData icon;
  final List<Widget> children;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Material(
        color: Theme.of(context).colorScheme.surface,
        borderRadius: BorderRadius.circular(8),
        clipBehavior: Clip.antiAlias,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(12, 8, 8, 4),
              child: Row(
                children: [
                  Icon(icon, size: 20, color: Theme.of(context).colorScheme.primary),
                  const SizedBox(width: 8),
                  Expanded(child: Text(title, style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700))),
                  if (trailing != null) trailing!,
                ],
              ),
            ),
            const Divider(height: 1),
            ...children,
          ],
        ),
      ),
    );
  }
}

class _ActionTile extends StatelessWidget {
  const _ActionTile({required this.icon, required this.title, required this.subtitle, required this.onTap});

  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return ListTile(
      dense: true,
      leading: Icon(icon),
      title: Text(title),
      subtitle: Text(subtitle, maxLines: 1, overflow: TextOverflow.ellipsis),
      trailing: const Icon(Icons.chevron_right_rounded),
      onTap: onTap,
    );
  }
}

class _SwitchTile extends StatelessWidget {
  const _SwitchTile({required this.title, required this.subtitle, required this.value, required this.onChanged});

  final String title;
  final String subtitle;
  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    return SwitchListTile(
      dense: true,
      title: Text(title),
      subtitle: Text(subtitle, maxLines: 1, overflow: TextOverflow.ellipsis),
      value: value,
      onChanged: onChanged,
    );
  }
}

class _ChoiceTile extends StatelessWidget {
  const _ChoiceTile({required this.title, required this.value, required this.choices, required this.onChanged});

  final String title;
  final String value;
  final Map<String, String> choices;
  final ValueChanged<String> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 8, 12, 8),
      child: Row(
        children: [
          Expanded(child: Text(title)),
          SegmentedButton<String>(
            segments: choices.entries.map((entry) => ButtonSegment<String>(value: entry.key, label: Text(entry.value))).toList(growable: false),
            selected: {value},
            showSelectedIcon: false,
            onSelectionChanged: (selection) => onChanged(selection.first),
          ),
        ],
      ),
    );
  }
}

class _SliderTile extends StatelessWidget {
  const _SliderTile({required this.title, required this.value, required this.min, required this.max, required this.onChanged});

  final String title;
  final int value;
  final int min;
  final int max;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 4, 12, 8),
      child: Row(
        children: [
          SizedBox(width: 96, child: Text('$title: $value')),
          Expanded(
            child: Slider(
              value: value.toDouble(),
              min: min.toDouble(),
              max: max.toDouble(),
              divisions: max - min,
              label: '$value',
              onChanged: (next) => onChanged(next.round().clamp(min, max).toInt()),
            ),
          ),
        ],
      ),
    );
  }
}

class _MiniStatus extends StatelessWidget {
  const _MiniStatus({required this.icon, required this.label});

  final IconData icon;
  final String label;

  @override
  Widget build(BuildContext context) {
    return Chip(
      visualDensity: VisualDensity.compact,
      avatar: Icon(icon, size: 16),
      label: Text(label),
    );
  }
}

class _InfoRow extends StatelessWidget {
  const _InfoRow(this.label, this.value);

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 8, 12, 8),
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

class _StatusDot extends StatelessWidget {
  const _StatusDot({required this.active});

  final bool active;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: active ? '文件权限已授权' : '文件权限未授权',
      child: Icon(active ? Icons.verified_rounded : Icons.error_outline_rounded, size: 18, color: active ? Colors.green : Theme.of(context).colorScheme.error),
    );
  }
}

class _EmptyState extends StatelessWidget {
  const _EmptyState({required this.icon, required this.message});

  final IconData icon;
  final String message;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 40, color: Theme.of(context).colorScheme.onSurfaceVariant),
            const SizedBox(height: 8),
            Text(message, style: Theme.of(context).textTheme.bodyMedium),
          ],
        ),
      ),
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
      borderRadius: BorderRadius.circular(8),
      child: Container(
        width: size,
        height: size,
        color: Theme.of(context).colorScheme.surfaceContainerHighest,
        child: image == null ? Icon(Icons.videogame_asset_outlined, size: size * 0.44) : Image(image: image, fit: BoxFit.cover),
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
