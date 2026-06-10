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

class _LauncherHomePageState extends State<LauncherHomePage> {
  final TextEditingController _rootController = TextEditingController(text: '/storage/emulated/0/krkr2pro');

  int _pageIndex = 0;
  List<GameEntry> _games = const [];
  GameEntry? _selectedGame;
  bool _loading = true;
  bool _storageGranted = false;
  String _scanStatus = '准备扫描';

  @override
  void initState() {
    super.initState();
    _loadInitialState();
  }

  @override
  void dispose() {
    _rootController.dispose();
    super.dispose();
  }

  Future<void> _loadInitialState() async {
    await _refreshPermission();
    try {
      final root = await widget.bridge.getGameRoot();
      if (mounted) {
        _rootController.text = root;
      }
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
        setState(() => _storageGranted = false);
      }
    }
  }

  Future<void> _requestPermission() async {
    try {
      await widget.bridge.requestFileManagementPermission();
    } catch (_) {
      _showSnack('当前平台暂未接入权限入口');
    }
    await _refreshPermission();
  }

  Future<void> _scanGames() async {
    final root = _normalizedRoot;
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

  String get _normalizedRoot {
    final root = _rootController.text.trim();
    return root.isEmpty ? '/storage/emulated/0/krkr2pro' : root;
  }

  Future<void> _pickGameRoot() async {
    try {
      await widget.bridge.pickGame();
    } catch (_) {
      _showSnack('目录选择暂未接入，请手动输入路径');
    }
  }

  Future<void> _launch(GameEntry game) async {
    if (game.path.isEmpty) {
      _showSnack('请选择有效游戏目录');
      return;
    }
    try {
      await widget.bridge.launchGame(game);
    } catch (_) {
      _showSnack('启动失败：${game.path}');
    }
  }

  Future<void> _openSettings() async {
    try {
      await widget.bridge.openSettings();
    } catch (_) {
      _showSnack('设置入口暂未接入');
    }
  }

  Future<void> _openDiagnostics() async {
    try {
      await widget.bridge.openDiagnostics();
    } catch (_) {
      _showSnack('诊断入口暂未接入');
    }
  }

  void _updateGame(GameEntry updated) {
    setState(() {
      _games = _games.map((game) => game.path == updated.path ? updated : game).toList(growable: false);
      _selectedGame = updated;
    });
  }

  void _showRootSheet() {
    showModalBottomSheet<void>(
      context: context,
      showDragHandle: true,
      isScrollControlled: true,
      builder: (context) {
        return Padding(
          padding: EdgeInsets.fromLTRB(20, 0, 20, MediaQuery.viewInsetsOf(context).bottom + 20),
          child: _RootSheet(
            controller: _rootController,
            onPickRoot: _pickGameRoot,
            onScan: () {
              Navigator.of(context).pop();
              _scanGames();
            },
          ),
        );
      },
    );
  }

  void _showSnack(String message) {
    if (!mounted) {
      return;
    }
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final mode = _LayoutModeX.fromWidth(constraints.maxWidth);
        final body = _pageIndex == 0
            ? _LibraryPage(
                mode: mode,
                loading: _loading,
                games: _games,
                selectedGame: _selectedGame,
                storageGranted: _storageGranted,
                scanStatus: _scanStatus,
                rootPath: _normalizedRoot,
                onRoot: _showRootSheet,
                onScan: _scanGames,
                onRequestPermission: _requestPermission,
                onSelectGame: (game) => setState(() => _selectedGame = game),
                onUpdateGame: _updateGame,
                onLaunch: _launch,
              )
            : _ToolsPage(
                storageGranted: _storageGranted,
                onRequestPermission: _requestPermission,
                onRoot: _showRootSheet,
                onScan: _scanGames,
                onOpenSettings: _openSettings,
                onOpenDiagnostics: _openDiagnostics,
              );

        return Scaffold(
          appBar: AppBar(
            title: const Text('KrKr2'),
            actions: [
              IconButton(onPressed: _showRootSheet, icon: const Icon(Icons.folder_open_outlined), tooltip: '游戏目录'),
              IconButton(onPressed: _scanGames, icon: const Icon(Icons.refresh), tooltip: '重新扫描'),
              IconButton(onPressed: _openSettings, icon: const Icon(Icons.settings_outlined), tooltip: '设置'),
            ],
          ),
          body: Row(
            children: [
              if (!mode.compact)
                _SideNavigation(
                  selectedIndex: _pageIndex,
                  onChanged: (index) => setState(() => _pageIndex = index),
                ),
              Expanded(child: body),
            ],
          ),
          bottomNavigationBar: mode.compact
              ? NavigationBar(
                  selectedIndex: _pageIndex,
                  onDestinationSelected: (index) => setState(() => _pageIndex = index),
                  destinations: const [
                    NavigationDestination(icon: Icon(Icons.grid_view_outlined), selectedIcon: Icon(Icons.grid_view_rounded), label: '游戏'),
                    NavigationDestination(icon: Icon(Icons.tune_outlined), selectedIcon: Icon(Icons.tune_rounded), label: '工具'),
                  ],
                )
              : null,
        );
      },
    );
  }
}

enum _LayoutMode { compact, medium, expanded }

extension _LayoutModeX on _LayoutMode {
  static _LayoutMode fromWidth(double width) {
    if (width >= 1080) {
      return _LayoutMode.expanded;
    }
    if (width >= 720) {
      return _LayoutMode.medium;
    }
    return _LayoutMode.compact;
  }

  bool get compact => this == _LayoutMode.compact;
  bool get expanded => this == _LayoutMode.expanded;
}

class _SideNavigation extends StatelessWidget {
  const _SideNavigation({required this.selectedIndex, required this.onChanged});

  final int selectedIndex;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return NavigationRail(
      selectedIndex: selectedIndex,
      onDestinationSelected: onChanged,
      labelType: NavigationRailLabelType.all,
      destinations: const [
        NavigationRailDestination(icon: Icon(Icons.grid_view_outlined), selectedIcon: Icon(Icons.grid_view_rounded), label: Text('游戏')),
        NavigationRailDestination(icon: Icon(Icons.tune_outlined), selectedIcon: Icon(Icons.tune_rounded), label: Text('工具')),
      ],
    );
  }
}

class _LibraryPage extends StatelessWidget {
  const _LibraryPage({
    required this.mode,
    required this.loading,
    required this.games,
    required this.selectedGame,
    required this.storageGranted,
    required this.scanStatus,
    required this.rootPath,
    required this.onRoot,
    required this.onScan,
    required this.onRequestPermission,
    required this.onSelectGame,
    required this.onUpdateGame,
    required this.onLaunch,
  });

  final _LayoutMode mode;
  final bool loading;
  final List<GameEntry> games;
  final GameEntry? selectedGame;
  final bool storageGranted;
  final String scanStatus;
  final String rootPath;
  final VoidCallback onRoot;
  final VoidCallback onScan;
  final VoidCallback onRequestPermission;
  final ValueChanged<GameEntry> onSelectGame;
  final ValueChanged<GameEntry> onUpdateGame;
  final ValueChanged<GameEntry> onLaunch;

  @override
  Widget build(BuildContext context) {
    final list = _GameList(
      loading: loading,
      games: games,
      selectedGame: selectedGame,
      compact: mode.compact,
      onSelectGame: onSelectGame,
      onLaunch: onLaunch,
      onRoot: onRoot,
      onRequestPermission: onRequestPermission,
      storageGranted: storageGranted,
    );

    return Padding(
      padding: EdgeInsets.fromLTRB(mode.compact ? 12 : 20, 0, mode.compact ? 12 : 20, 16),
      child: Column(
        children: [
          _OverviewStrip(
            storageGranted: storageGranted,
            gameCount: games.length,
            scanStatus: scanStatus,
            rootPath: rootPath,
            onRoot: onRoot,
            onScan: onScan,
            onRequestPermission: onRequestPermission,
          ),
          const SizedBox(height: 12),
          Expanded(
            child: mode.expanded
                ? Row(
                    children: [
                      Expanded(flex: 3, child: list),
                      const SizedBox(width: 12),
                      Expanded(
                        flex: 2,
                        child: selectedGame == null
                            ? _InfoCard.empty(title: '选择游戏', message: '从左侧游戏列表中选择一个条目。')
                            : _GameDetail(game: selectedGame!, onUpdateGame: onUpdateGame, onLaunch: onLaunch),
                      ),
                    ],
                  )
                : list,
          ),
        ],
      ),
    );
  }
}

class _OverviewStrip extends StatelessWidget {
  const _OverviewStrip({
    required this.storageGranted,
    required this.gameCount,
    required this.scanStatus,
    required this.rootPath,
    required this.onRoot,
    required this.onScan,
    required this.onRequestPermission,
  });

  final bool storageGranted;
  final int gameCount;
  final String scanStatus;
  final String rootPath;
  final VoidCallback onRoot;
  final VoidCallback onScan;
  final VoidCallback onRequestPermission;

  @override
  Widget build(BuildContext context) {
    return _LauncherCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _SectionHeader(
            icon: Icons.dashboard_outlined,
            title: '概览',
            actions: [
              IconButton(onPressed: onScan, icon: const Icon(Icons.refresh), tooltip: '重新扫描'),
              IconButton(onPressed: onRoot, icon: const Icon(Icons.folder_open_outlined), tooltip: '游戏目录'),
            ],
          ),
          const SizedBox(height: 8),
          Wrap(
            spacing: 10,
            runSpacing: 10,
            children: [
              _InfoPill(icon: storageGranted ? Icons.verified_outlined : Icons.folder_off_outlined, label: storageGranted ? '文件权限已授权' : '需要文件权限', onTap: storageGranted ? null : onRequestPermission),
              _InfoPill(icon: Icons.sports_esports_outlined, label: '$gameCount 个游戏'),
              _InfoPill(icon: Icons.radar_outlined, label: scanStatus),
            ],
          ),
          const SizedBox(height: 10),
          Text(rootPath, maxLines: 1, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.bodySmall?.copyWith(color: Theme.of(context).colorScheme.onSurfaceVariant)),
        ],
      ),
    );
  }
}

class _GameList extends StatelessWidget {
  const _GameList({
    required this.loading,
    required this.games,
    required this.selectedGame,
    required this.compact,
    required this.onSelectGame,
    required this.onLaunch,
    required this.onRoot,
    required this.onRequestPermission,
    required this.storageGranted,
  });

  final bool loading;
  final List<GameEntry> games;
  final GameEntry? selectedGame;
  final bool compact;
  final ValueChanged<GameEntry> onSelectGame;
  final ValueChanged<GameEntry> onLaunch;
  final VoidCallback onRoot;
  final VoidCallback onRequestPermission;
  final bool storageGranted;

  @override
  Widget build(BuildContext context) {
    return _LauncherCard(
      child: Column(
        children: [
          const _SectionHeader(icon: Icons.grid_view_outlined, title: '游戏库'),
          const SizedBox(height: 8),
          Expanded(
            child: loading
                ? const Center(child: CircularProgressIndicator())
                : games.isEmpty
                    ? _EmptyState(onRoot: onRoot, onRequestPermission: onRequestPermission, storageGranted: storageGranted)
                    : ListView.separated(
                        itemCount: games.length,
                        separatorBuilder: (_, __) => const SizedBox(height: 8),
                        itemBuilder: (context, index) {
                          final game = games[index];
                          return _GameRow(
                            game: game,
                            selected: selectedGame?.path == game.path,
                            compact: compact,
                            onTap: () => onSelectGame(game),
                            onLaunch: () => onLaunch(game),
                          );
                        },
                      ),
          ),
        ],
      ),
    );
  }
}

class _GameRow extends StatelessWidget {
  const _GameRow({required this.game, required this.selected, required this.compact, required this.onTap, required this.onLaunch});

  final GameEntry game;
  final bool selected;
  final bool compact;
  final VoidCallback onTap;
  final VoidCallback onLaunch;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final image = _fileImage(game.coverPath ?? game.backgroundPath);
    return Material(
      color: selected ? scheme.secondaryContainer : scheme.surfaceContainerHighest.withOpacity(0.55),
      borderRadius: BorderRadius.circular(18),
      child: InkWell(
        borderRadius: BorderRadius.circular(18),
        onTap: onTap,
        onDoubleTap: onLaunch,
        child: Padding(
          padding: const EdgeInsets.all(10),
          child: Row(
            children: [
              Container(
                width: compact ? 48 : 58,
                height: compact ? 48 : 58,
                decoration: BoxDecoration(borderRadius: BorderRadius.circular(14), color: scheme.surfaceContainer),
                clipBehavior: Clip.antiAlias,
                child: image == null ? const Center(child: ResourceIcon('windows_icon.png')) : Image(image: image, fit: BoxFit.cover),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(game.title, maxLines: 1, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700)),
                    const SizedBox(height: 4),
                    Text(game.path, maxLines: 1, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.bodySmall?.copyWith(color: scheme.onSurfaceVariant)),
                  ],
                ),
              ),
              const SizedBox(width: 8),
              IconButton(onPressed: onLaunch, icon: const Icon(Icons.play_arrow_rounded), tooltip: '启动'),
            ],
          ),
        ),
      ),
    );
  }
}

class _GameDetail extends StatelessWidget {
  const _GameDetail({required this.game, required this.onUpdateGame, required this.onLaunch});

  final GameEntry game;
  final ValueChanged<GameEntry> onUpdateGame;
  final ValueChanged<GameEntry> onLaunch;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return _LauncherCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const _SectionHeader(icon: Icons.info_outline, title: '详情'),
          const SizedBox(height: 12),
          Text(game.title, style: Theme.of(context).textTheme.headlineSmall?.copyWith(fontWeight: FontWeight.w800)),
          const SizedBox(height: 8),
          Text(game.path, style: Theme.of(context).textTheme.bodySmall?.copyWith(color: scheme.onSurfaceVariant)),
          const SizedBox(height: 16),
          _LaunchFilePicker(game: game, onChanged: onUpdateGame),
          const Spacer(),
          SizedBox(width: double.infinity, child: FilledButton.icon(onPressed: () => onLaunch(game), icon: const Icon(Icons.play_arrow_rounded), label: const Text('启动游戏'))),
        ],
      ),
    );
  }
}

class _ToolsPage extends StatelessWidget {
  const _ToolsPage({required this.storageGranted, required this.onRequestPermission, required this.onRoot, required this.onScan, required this.onOpenSettings, required this.onOpenDiagnostics});

  final bool storageGranted;
  final VoidCallback onRequestPermission;
  final VoidCallback onRoot;
  final VoidCallback onScan;
  final VoidCallback onOpenSettings;
  final VoidCallback onOpenDiagnostics;

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 0, 20, 16),
      children: [
        _LauncherCard(
          child: Column(
            children: [
              const _SectionHeader(icon: Icons.tune_outlined, title: '工具'),
              _ActionTile(icon: storageGranted ? Icons.folder_special_outlined : Icons.folder_off_outlined, title: storageGranted ? '文件权限已授权' : '申请文件权限', subtitle: '用于扫描外部存储中的游戏目录', onTap: storageGranted ? null : onRequestPermission),
              _ActionTile(icon: Icons.folder_open_outlined, title: '游戏目录', subtitle: '修改根目录并重新扫描', onTap: onRoot),
              _ActionTile(icon: Icons.refresh, title: '重新扫描', subtitle: '刷新当前游戏库', onTap: onScan),
              _ActionTile(icon: Icons.settings_outlined, title: '设置', subtitle: '打开平台设置入口', onTap: onOpenSettings),
              _ActionTile(icon: Icons.bug_report_outlined, title: '诊断', subtitle: '查看日志和环境信息', onTap: onOpenDiagnostics),
            ],
          ),
        ),
      ],
    );
  }
}

class _ActionTile extends StatelessWidget {
  const _ActionTile({required this.icon, required this.title, required this.subtitle, required this.onTap});

  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    return ListTile(leading: Icon(icon), title: Text(title), subtitle: Text(subtitle), onTap: onTap);
  }
}

class _RootSheet extends StatelessWidget {
  const _RootSheet({required this.controller, required this.onPickRoot, required this.onScan});

  final TextEditingController controller;
  final VoidCallback onPickRoot;
  final VoidCallback onScan;

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('游戏根目录', style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w700)),
        const SizedBox(height: 12),
        TextField(controller: controller, decoration: const InputDecoration(prefixIcon: Icon(Icons.folder_outlined), hintText: '/storage/emulated/0/krkr2pro'), onSubmitted: (_) => onScan()),
        const SizedBox(height: 12),
        Row(children: [
          Expanded(child: OutlinedButton.icon(onPressed: onPickRoot, icon: const Icon(Icons.folder_open_outlined), label: const Text('选择'))),
          const SizedBox(width: 10),
          Expanded(child: FilledButton.icon(onPressed: onScan, icon: const Icon(Icons.search), label: const Text('扫描'))),
        ]),
      ],
    );
  }
}

class _EmptyState extends StatelessWidget {
  const _EmptyState({required this.onRoot, required this.onRequestPermission, required this.storageGranted});

  final VoidCallback onRoot;
  final VoidCallback onRequestPermission;
  final bool storageGranted;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const ResourceIcon('empty.png', size: 56),
            const SizedBox(height: 14),
            Text('没有找到游戏', style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w700)),
            const SizedBox(height: 8),
            const Text('请选择包含 startup.tjs 或 data.xp3 的游戏目录。', textAlign: TextAlign.center),
            const SizedBox(height: 16),
            Wrap(spacing: 10, runSpacing: 10, alignment: WrapAlignment.center, children: [
              FilledButton.icon(onPressed: onRoot, icon: const Icon(Icons.folder_open_outlined), label: const Text('游戏目录')),
              if (!storageGranted) OutlinedButton.icon(onPressed: onRequestPermission, icon: const Icon(Icons.admin_panel_settings_outlined), label: const Text('授权文件')),
            ]),
          ],
        ),
      ),
    );
  }
}

class _InfoCard extends StatelessWidget {
  const _InfoCard.empty({required this.title, required this.message});

  final String title;
  final String message;

  @override
  Widget build(BuildContext context) {
    return _LauncherCard(
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const ResourceIcon('empty.png', size: 48),
            const SizedBox(height: 12),
            Text(title, style: Theme.of(context).textTheme.titleLarge),
            const SizedBox(height: 6),
            Text(message, textAlign: TextAlign.center),
          ],
        ),
      ),
    );
  }
}

class _LauncherCard extends StatelessWidget {
  const _LauncherCard({required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Card(child: Padding(padding: const EdgeInsets.all(16), child: child));
  }
}

class _SectionHeader extends StatelessWidget {
  const _SectionHeader({required this.icon, required this.title, this.actions = const []});

  final IconData icon;
  final String title;
  final List<Widget> actions;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 20, color: Theme.of(context).colorScheme.onSurfaceVariant),
        const SizedBox(width: 8),
        Expanded(child: Text(title, style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700))),
        ...actions,
      ],
    );
  }
}

class _InfoPill extends StatelessWidget {
  const _InfoPill({required this.icon, required this.label, this.onTap});

  final IconData icon;
  final String label;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    return ActionChip(avatar: Icon(icon, size: 18), label: Text(label), onPressed: onTap);
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
          items: values.map((path) {
            final label = path.isEmpty ? '自动检测' : _relativeToGame(path, widget.game.path);
            return DropdownMenuItem(value: path, child: Text(label, overflow: TextOverflow.ellipsis));
          }).toList(growable: false),
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
