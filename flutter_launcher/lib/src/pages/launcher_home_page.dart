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
  final GlobalKey<ScaffoldState> _scaffoldKey = GlobalKey<ScaffoldState>();

  List<GameEntry> _games = const [];
  GameEntry? _selectedGame;
  bool _loading = true;
  bool _storageGranted = false;
  String _scanStatus = '准备扫描游戏目录';

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
    final root = _rootController.text.trim().isEmpty ? '/storage/emulated/0/krkr2pro' : _rootController.text.trim();
    setState(() {
      _loading = true;
      _scanStatus = '扫描中：$root';
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
        _selectedGame = games.isEmpty ? null : (_selectedGame == null ? games.first : games.firstWhere((game) => game.path == _selectedGame!.path, orElse: () => games.first));
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

  void _updateGame(GameEntry updated) {
    setState(() {
      _games = _games.map((game) => game.path == updated.path ? updated : game).toList(growable: false);
      _selectedGame = updated;
    });
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

  void _showRootSheet() {
    showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      backgroundColor: Theme.of(context).colorScheme.surface,
      showDragHandle: true,
      builder: (context) {
        return Padding(
          padding: EdgeInsets.fromLTRB(20, 4, 20, MediaQuery.viewInsetsOf(context).bottom + 20),
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
    return Scaffold(
      key: _scaffoldKey,
      drawer: _LauncherDrawer(
        storageGranted: _storageGranted,
        onRequestPermission: _requestPermission,
        onOpenSettings: _openSettings,
        onOpenDiagnostics: _openDiagnostics,
      ),
      body: _LauncherBackground(
        child: SafeArea(
          child: LayoutBuilder(
            builder: (context, constraints) {
              final layout = _LauncherLayoutX.fromWidth(constraints.maxWidth);
              return Row(
                children: [
                  if (layout.showRail)
                    _LauncherSideBar(
                      storageGranted: _storageGranted,
                      onRequestPermission: _requestPermission,
                      onOpenSettings: _openSettings,
                      onOpenDiagnostics: _openDiagnostics,
                    ),
                  Expanded(
                    child: _LauncherBody(
                      layout: layout,
                      games: _games,
                      selectedGame: _selectedGame,
                      loading: _loading,
                      storageGranted: _storageGranted,
                      scanStatus: _scanStatus,
                      rootPath: _rootController.text,
                      onMenu: () => _scaffoldKey.currentState?.openDrawer(),
                      onRoot: _showRootSheet,
                      onScan: _scanGames,
                      onRequestPermission: _requestPermission,
                      onSelectGame: (game) => setState(() => _selectedGame = game),
                      onUpdateGame: _updateGame,
                      onLaunch: _launch,
                      onOpenSettings: _openSettings,
                      onOpenDiagnostics: _openDiagnostics,
                    ),
                  ),
                ],
              );
            },
          ),
        ),
      ),
    );
  }
}

enum _LauncherLayout { compact, medium, expanded }

extension _LauncherLayoutX on _LauncherLayout {
  static _LauncherLayout fromWidth(double width) {
    if (width >= 1100) {
      return _LauncherLayout.expanded;
    }
    if (width >= 720) {
      return _LauncherLayout.medium;
    }
    return _LauncherLayout.compact;
  }

  bool get showRail => this == _LauncherLayout.expanded;
  bool get showDetail => this == _LauncherLayout.expanded;
  bool get compact => this == _LauncherLayout.compact;
}

class _LauncherBackground extends StatelessWidget {
  const _LauncherBackground({required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return DecoratedBox(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [scheme.surface, scheme.surfaceContainerHighest, scheme.primaryContainer.withOpacity(0.32)],
        ),
      ),
      child: child,
    );
  }
}

class _LauncherBody extends StatelessWidget {
  const _LauncherBody({
    required this.layout,
    required this.games,
    required this.selectedGame,
    required this.loading,
    required this.storageGranted,
    required this.scanStatus,
    required this.rootPath,
    required this.onMenu,
    required this.onRoot,
    required this.onScan,
    required this.onRequestPermission,
    required this.onSelectGame,
    required this.onUpdateGame,
    required this.onLaunch,
    required this.onOpenSettings,
    required this.onOpenDiagnostics,
  });

  final _LauncherLayout layout;
  final List<GameEntry> games;
  final GameEntry? selectedGame;
  final bool loading;
  final bool storageGranted;
  final String scanStatus;
  final String rootPath;
  final VoidCallback onMenu;
  final VoidCallback onRoot;
  final VoidCallback onScan;
  final VoidCallback onRequestPermission;
  final ValueChanged<GameEntry> onSelectGame;
  final ValueChanged<GameEntry> onUpdateGame;
  final ValueChanged<GameEntry> onLaunch;
  final VoidCallback onOpenSettings;
  final VoidCallback onOpenDiagnostics;

  @override
  Widget build(BuildContext context) {
    final content = CustomScrollView(
      slivers: [
        SliverPadding(
          padding: EdgeInsets.fromLTRB(layout.compact ? 16 : 24, 18, layout.compact ? 16 : 24, 0),
          sliver: SliverToBoxAdapter(
            child: _HeaderBar(
              compact: layout.compact,
              storageGranted: storageGranted,
              scanStatus: scanStatus,
              gameCount: games.length,
              rootPath: rootPath,
              onMenu: onMenu,
              onRoot: onRoot,
              onScan: onScan,
              onRequestPermission: onRequestPermission,
              onOpenSettings: onOpenSettings,
              onOpenDiagnostics: onOpenDiagnostics,
            ),
          ),
        ),
        if (loading)
          const SliverFillRemaining(hasScrollBody: false, child: Center(child: CircularProgressIndicator()))
        else if (games.isEmpty)
          SliverFillRemaining(
            hasScrollBody: false,
            child: Padding(
              padding: EdgeInsets.all(layout.compact ? 16 : 24),
              child: _EmptyLauncherState(onRoot: onRoot, onRequestPermission: onRequestPermission, storageGranted: storageGranted),
            ),
          )
        else
          SliverPadding(
            padding: EdgeInsets.all(layout.compact ? 16 : 24),
            sliver: _GameGridSliver(
              compact: layout.compact,
              games: games,
              selectedGame: selectedGame,
              onSelectGame: onSelectGame,
              onLaunch: onLaunch,
            ),
          ),
      ],
    );

    if (!layout.showDetail) {
      return content;
    }

    return Row(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Expanded(child: content),
        SizedBox(
          width: 390,
          child: Padding(
            padding: const EdgeInsets.fromLTRB(0, 18, 24, 24),
            child: selectedGame == null
                ? _NoSelectionPanel(onRoot: onRoot)
                : _GameDetailPanel(game: selectedGame!, onUpdateGame: onUpdateGame, onLaunch: onLaunch),
          ),
        ),
      ],
    );
  }
}

class _HeaderBar extends StatelessWidget {
  const _HeaderBar({
    required this.compact,
    required this.storageGranted,
    required this.scanStatus,
    required this.gameCount,
    required this.rootPath,
    required this.onMenu,
    required this.onRoot,
    required this.onScan,
    required this.onRequestPermission,
    required this.onOpenSettings,
    required this.onOpenDiagnostics,
  });

  final bool compact;
  final bool storageGranted;
  final String scanStatus;
  final int gameCount;
  final String rootPath;
  final VoidCallback onMenu;
  final VoidCallback onRoot;
  final VoidCallback onScan;
  final VoidCallback onRequestPermission;
  final VoidCallback onOpenSettings;
  final VoidCallback onOpenDiagnostics;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Card(
      color: scheme.surface.withOpacity(0.88),
      child: Padding(
        padding: EdgeInsets.all(compact ? 16 : 20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                if (compact) IconButton(onPressed: onMenu, icon: const Icon(Icons.menu)),
                Container(
                  width: 46,
                  height: 46,
                  decoration: BoxDecoration(color: scheme.primaryContainer, borderRadius: BorderRadius.circular(16)),
                  child: const Center(child: ResourceIcon('menu_icon.png', size: 28)),
                ),
                const SizedBox(width: 14),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('KrKr2 启动器', style: Theme.of(context).textTheme.headlineSmall?.copyWith(fontWeight: FontWeight.w800)),
                      const SizedBox(height: 2),
                      Text(rootPath, maxLines: 1, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.bodySmall?.copyWith(color: scheme.onSurfaceVariant)),
                    ],
                  ),
                ),
                if (!compact) ...[
                  _HeaderIconButton(icon: Icons.settings_outlined, label: '设置', onPressed: onOpenSettings),
                  _HeaderIconButton(icon: Icons.bug_report_outlined, label: '诊断', onPressed: onOpenDiagnostics),
                ],
              ],
            ),
            const SizedBox(height: 16),
            Wrap(
              spacing: 10,
              runSpacing: 10,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                _StatusChip(icon: storageGranted ? Icons.verified_rounded : Icons.folder_off_outlined, label: storageGranted ? '文件权限已授权' : '需要文件管理权限', active: storageGranted),
                _StatusChip(icon: Icons.grid_view_rounded, label: '$gameCount 个游戏', active: gameCount > 0),
                _StatusChip(icon: Icons.radar_rounded, label: scanStatus, active: gameCount > 0),
              ],
            ),
            const SizedBox(height: 16),
            Wrap(
              spacing: 10,
              runSpacing: 10,
              children: [
                FilledButton.icon(onPressed: onRoot, icon: const Icon(Icons.folder_open_rounded), label: const Text('游戏目录')),
                OutlinedButton.icon(onPressed: onScan, icon: const Icon(Icons.refresh_rounded), label: const Text('重新扫描')),
                if (!storageGranted) OutlinedButton.icon(onPressed: onRequestPermission, icon: const Icon(Icons.admin_panel_settings_outlined), label: const Text('授权文件')),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _HeaderIconButton extends StatelessWidget {
  const _HeaderIconButton({required this.icon, required this.label, required this.onPressed});

  final IconData icon;
  final String label;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: label,
      child: IconButton(onPressed: onPressed, icon: Icon(icon)),
    );
  }
}

class _StatusChip extends StatelessWidget {
  const _StatusChip({required this.icon, required this.label, required this.active});

  final IconData icon;
  final String label;
  final bool active;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: active ? scheme.primaryContainer.withOpacity(0.72) : scheme.surfaceContainerHighest,
        borderRadius: BorderRadius.circular(999),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [Icon(icon, size: 17), const SizedBox(width: 6), Text(label, style: Theme.of(context).textTheme.labelMedium)],
      ),
    );
  }
}

class _GameGridSliver extends StatelessWidget {
  const _GameGridSliver({required this.compact, required this.games, required this.selectedGame, required this.onSelectGame, required this.onLaunch});

  final bool compact;
  final List<GameEntry> games;
  final GameEntry? selectedGame;
  final ValueChanged<GameEntry> onSelectGame;
  final ValueChanged<GameEntry> onLaunch;

  @override
  Widget build(BuildContext context) {
    if (compact) {
      return SliverList(
        delegate: SliverChildBuilderDelegate(
          (context, index) {
            final gameIndex = index ~/ 2;
            if (index.isOdd) {
              return const SizedBox(height: 10);
            }
            final game = games[gameIndex];
            return _GameListTile(game: game, selected: selectedGame?.path == game.path, onSelect: () => onSelectGame(game), onLaunch: () => onLaunch(game));
          },
          childCount: games.isEmpty ? 0 : games.length * 2 - 1,
        ),
      );
    }
    return SliverGrid.builder(
      gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(maxCrossAxisExtent: 260, mainAxisSpacing: 16, crossAxisSpacing: 16, childAspectRatio: 0.82),
      itemCount: games.length,
      itemBuilder: (context, index) {
        final game = games[index];
        return _GamePosterCard(game: game, selected: selectedGame?.path == game.path, onSelect: () => onSelectGame(game), onLaunch: () => onLaunch(game));
      },
    );
  }
}

class _GamePosterCard extends StatelessWidget {
  const _GamePosterCard({required this.game, required this.selected, required this.onSelect, required this.onLaunch});

  final GameEntry game;
  final bool selected;
  final VoidCallback onSelect;
  final VoidCallback onLaunch;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Card(
      clipBehavior: Clip.antiAlias,
      color: selected ? scheme.primaryContainer : scheme.surface.withOpacity(0.9),
      child: InkWell(
        onTap: onSelect,
        onDoubleTap: onLaunch,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Expanded(flex: 7, child: _GameArtwork(game: game, radius: 0)),
            Expanded(
              flex: 4,
              child: Padding(
                padding: const EdgeInsets.all(14),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(game.title, maxLines: 2, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700)),
                    const SizedBox(height: 6),
                    Text(game.path, maxLines: 1, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.bodySmall?.copyWith(color: scheme.onSurfaceVariant)),
                    const Spacer(),
                    Align(alignment: Alignment.centerRight, child: IconButton.filled(onPressed: onLaunch, icon: const Icon(Icons.play_arrow_rounded))),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _GameListTile extends StatelessWidget {
  const _GameListTile({required this.game, required this.selected, required this.onSelect, required this.onLaunch});

  final GameEntry game;
  final bool selected;
  final VoidCallback onSelect;
  final VoidCallback onLaunch;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Card(
      color: selected ? scheme.primaryContainer : scheme.surface.withOpacity(0.9),
      child: ListTile(
        onTap: onSelect,
        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
        leading: ClipRRect(borderRadius: BorderRadius.circular(14), child: SizedBox(width: 58, height: 58, child: _GameArtwork(game: game, radius: 14))),
        title: Text(game.title, maxLines: 1, overflow: TextOverflow.ellipsis, style: const TextStyle(fontWeight: FontWeight.w700)),
        subtitle: Text(game.path, maxLines: 1, overflow: TextOverflow.ellipsis),
        trailing: IconButton.filled(onPressed: onLaunch, icon: const Icon(Icons.play_arrow_rounded)),
      ),
    );
  }
}

class _GameArtwork extends StatelessWidget {
  const _GameArtwork({required this.game, required this.radius});

  final GameEntry game;
  final double radius;

  @override
  Widget build(BuildContext context) {
    final image = _fileImage(game.coverPath ?? game.backgroundPath);
    final scheme = Theme.of(context).colorScheme;
    return DecoratedBox(
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(radius),
        gradient: LinearGradient(colors: [scheme.primaryContainer, scheme.secondaryContainer]),
      ),
      child: image == null
          ? const Center(child: ResourceIcon('windows_icon.png', size: 36))
          : Image(image: image, fit: BoxFit.cover, errorBuilder: (_, __, ___) => const Center(child: ResourceIcon('windows_icon.png', size: 36))),
    );
  }
}

class _GameDetailPanel extends StatelessWidget {
  const _GameDetailPanel({required this.game, required this.onUpdateGame, required this.onLaunch});

  final GameEntry game;
  final ValueChanged<GameEntry> onUpdateGame;
  final ValueChanged<GameEntry> onLaunch;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Card(
      clipBehavior: Clip.antiAlias,
      color: scheme.surface.withOpacity(0.9),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SizedBox(height: 190, child: _GameArtwork(game: game, radius: 0)),
          Expanded(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(18),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(game.title, style: Theme.of(context).textTheme.headlineSmall?.copyWith(fontWeight: FontWeight.w800)),
                  const SizedBox(height: 8),
                  Text(game.path, style: Theme.of(context).textTheme.bodySmall?.copyWith(color: scheme.onSurfaceVariant)),
                  const SizedBox(height: 18),
                  _LaunchFilePicker(game: game, onChanged: onUpdateGame),
                  const SizedBox(height: 18),
                  SizedBox(width: double.infinity, child: FilledButton.icon(onPressed: () => onLaunch(game), icon: const Icon(Icons.play_arrow_rounded), label: const Text('启动游戏'))),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _NoSelectionPanel extends StatelessWidget {
  const _NoSelectionPanel({required this.onRoot});

  final VoidCallback onRoot;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Center(
        child: Padding(
          padding: const EdgeInsets.all(28),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const ResourceIcon('empty.png', size: 54),
              const SizedBox(height: 14),
              Text('选择一个游戏', style: Theme.of(context).textTheme.titleLarge),
              const SizedBox(height: 8),
              const Text('从游戏库中选择条目查看详情。', textAlign: TextAlign.center),
              const SizedBox(height: 18),
              OutlinedButton.icon(onPressed: onRoot, icon: const Icon(Icons.folder_open_rounded), label: const Text('游戏目录')),
            ],
          ),
        ),
      ),
    );
  }
}

class _EmptyLauncherState extends StatelessWidget {
  const _EmptyLauncherState({required this.onRoot, required this.onRequestPermission, required this.storageGranted});

  final VoidCallback onRoot;
  final VoidCallback onRequestPermission;
  final bool storageGranted;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Center(
        child: Padding(
          padding: const EdgeInsets.all(28),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const ResourceIcon('empty.png', size: 64),
              const SizedBox(height: 16),
              Text('没有找到游戏', style: Theme.of(context).textTheme.headlineSmall?.copyWith(fontWeight: FontWeight.w800)),
              const SizedBox(height: 8),
              const Text('请选择包含 startup.tjs、data.xp3 或 scenario.ks 的游戏目录。', textAlign: TextAlign.center),
              const SizedBox(height: 18),
              Wrap(
                spacing: 10,
                runSpacing: 10,
                alignment: WrapAlignment.center,
                children: [
                  FilledButton.icon(onPressed: onRoot, icon: const Icon(Icons.folder_open_rounded), label: const Text('选择目录')),
                  if (!storageGranted) OutlinedButton.icon(onPressed: onRequestPermission, icon: const Icon(Icons.admin_panel_settings_outlined), label: const Text('授权文件')),
                ],
              ),
            ],
          ),
        ),
      ),
    );
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
        Text('游戏根目录', style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800)),
        const SizedBox(height: 12),
        TextField(
          controller: controller,
          decoration: const InputDecoration(border: OutlineInputBorder(), prefixIcon: Icon(Icons.folder_outlined), hintText: '/storage/emulated/0/krkr2pro'),
          onSubmitted: (_) => onScan(),
        ),
        const SizedBox(height: 12),
        Row(
          children: [
            Expanded(child: OutlinedButton.icon(onPressed: onPickRoot, icon: const Icon(Icons.folder_open_rounded), label: const Text('选择'))),
            const SizedBox(width: 10),
            Expanded(child: FilledButton.icon(onPressed: onScan, icon: const Icon(Icons.search_rounded), label: const Text('扫描'))),
          ],
        ),
      ],
    );
  }
}

class _LauncherSideBar extends StatelessWidget {
  const _LauncherSideBar({required this.storageGranted, required this.onRequestPermission, required this.onOpenSettings, required this.onOpenDiagnostics});

  final bool storageGranted;
  final VoidCallback onRequestPermission;
  final VoidCallback onOpenSettings;
  final VoidCallback onOpenDiagnostics;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      width: 88,
      margin: const EdgeInsets.all(18),
      decoration: BoxDecoration(color: scheme.surface.withOpacity(0.78), borderRadius: BorderRadius.circular(28)),
      child: Column(
        children: [
          const SizedBox(height: 18),
          const ResourceIcon('menu_icon.png', size: 34),
          const Spacer(),
          IconButton(onPressed: storageGranted ? null : onRequestPermission, icon: Icon(storageGranted ? Icons.folder_special_rounded : Icons.folder_off_outlined), tooltip: '文件权限'),
          IconButton(onPressed: onOpenSettings, icon: const Icon(Icons.settings_outlined), tooltip: '设置'),
          IconButton(onPressed: onOpenDiagnostics, icon: const Icon(Icons.bug_report_outlined), tooltip: '诊断'),
          const SizedBox(height: 18),
        ],
      ),
    );
  }
}

class _LauncherDrawer extends StatelessWidget {
  const _LauncherDrawer({required this.storageGranted, required this.onRequestPermission, required this.onOpenSettings, required this.onOpenDiagnostics});

  final bool storageGranted;
  final VoidCallback onRequestPermission;
  final VoidCallback onOpenSettings;
  final VoidCallback onOpenDiagnostics;

  @override
  Widget build(BuildContext context) {
    return NavigationDrawer(
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(24, 24, 24, 12),
          child: Row(children: [const ResourceIcon('menu_icon.png', size: 34), const SizedBox(width: 12), Text('KrKr2', style: Theme.of(context).textTheme.titleLarge)]),
        ),
        ListTile(leading: Icon(storageGranted ? Icons.folder_special_rounded : Icons.folder_off_outlined), title: Text(storageGranted ? '文件权限已授权' : '申请文件权限'), onTap: storageGranted ? null : onRequestPermission),
        ListTile(leading: const Icon(Icons.settings_outlined), title: const Text('设置'), onTap: onOpenSettings),
        ListTile(leading: const Icon(Icons.bug_report_outlined), title: const Text('诊断'), onTap: onOpenDiagnostics),
      ],
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
          decoration: const InputDecoration(labelText: '启动文件', border: OutlineInputBorder()),
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
