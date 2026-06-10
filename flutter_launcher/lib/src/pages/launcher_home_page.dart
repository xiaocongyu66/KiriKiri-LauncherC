import 'dart:io';

import 'package:flutter/material.dart';

import '../bridge/launcher_bridge.dart';
import '../models/game_entry.dart';
import '../widgets/resource_icon.dart';

enum _LauncherDest { library, settings, tools }

class LauncherHomePage extends StatefulWidget {
  const LauncherHomePage({required this.bridge, super.key});

  final LauncherBridge bridge;

  @override
  State<LauncherHomePage> createState() => _LauncherHomePageState();
}

class _LauncherHomePageState extends State<LauncherHomePage> {
  final TextEditingController _rootController = TextEditingController(text: '/storage/emulated/0/krkr2pro');
  _LauncherDest _dest = _LauncherDest.library;
  List<GameEntry> _games = const [];
  GameEntry? _selectedGame;
  bool _loading = true;
  bool _storageGranted = false;
  String _scanStatus = '准备扫描游戏目录';

  @override
  void initState() {
    super.initState();
    _bootstrapFromOldPrefs();
  }

  @override
  void dispose() {
    _rootController.dispose();
    super.dispose();
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

  Future<void> _bootstrapFromOldPrefs() async {
    await _refreshPermission();
    try {
      final root = await widget.bridge.getGameRoot();
      if (mounted) {
        _rootController.text = root;
      }
    } catch (_) {
      // Keep old default path.
    }
    await _scanGames();
  }

  Future<void> _requestPermission() async {
    try {
      await widget.bridge.requestFileManagementPermission();
    } catch (_) {
      _showSnack('当前平台权限桥未接入');
    }
    await _refreshPermission();
  }

  Future<void> _scanGames() async {
    setState(() {
      _loading = true;
      _scanStatus = '正在扫描 ${_rootController.text.trim().isEmpty ? '/storage/emulated/0/krkr2pro' : _rootController.text.trim()}';
    });
    try {
      final root = _rootController.text.trim().isEmpty ? '/storage/emulated/0/krkr2pro' : _rootController.text.trim();
      await widget.bridge.setGameRoot(root);
      final depth = await widget.bridge.getScanDepth();
      final games = await widget.bridge.scanGames(rootPath: root, maxDepth: depth);
      if (!mounted) {
        return;
      }
      setState(() {
        _games = games;
        _selectedGame = games.isNotEmpty ? games.first : null;
        _scanStatus = games.isEmpty ? '没有发现游戏，请检查根目录或权限' : '发现 ${games.length} 个游戏';
      });
    } catch (_) {
      if (!mounted) {
        return;
      }
      setState(() {
        _games = const [];
        _selectedGame = null;
        _scanStatus = '扫描桥接尚未接入，先保留旧启动器功能入口';
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
      await _scanGames();
    } catch (_) {
      _showSnack('目录选择桥接尚未接入，请先手动填写路径');
    }
  }

  void _showRootConfig() {
    showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      builder: (context) => Padding(
        padding: EdgeInsets.only(
          left: 16,
          right: 16,
          top: 16,
          bottom: MediaQuery.viewInsetsOf(context).bottom + 16,
        ),
        child: _RootConfigCard(
          controller: _rootController,
          onPickRoot: _pickGameRoot,
          onScan: () {
            Navigator.of(context).pop();
            _scanGames();
          },
        ),
      ),
    );
  }

  Future<void> _launch(GameEntry game) async {
    if (game.path.isEmpty) {
      _showSnack('请先选择有效游戏目录');
      return;
    }
    try {
      await widget.bridge.launchGame(game);
    } catch (_) {
      _showSnack('C API 启动桥接失败：${game.path}');
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
      _showSnack('原生设置页桥接尚未接入');
    }
  }

  Future<void> _openDiagnostics() async {
    try {
      await widget.bridge.openDiagnostics();
    } catch (_) {
      _showSnack('诊断页桥接尚未接入');
    }
  }

  void _showSnack(String message) {
    if (!mounted) {
      return;
    }
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
  }

  void _selectDest(_LauncherDest dest) {
    setState(() => _dest = dest);
    if (dest == _LauncherDest.settings) {
      _openSettings();
    } else if (dest == _LauncherDest.tools) {
      _openDiagnostics();
    }
  }

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.sizeOf(context).width;
    final expanded = width >= 840;
    return Scaffold(
      body: Row(
        children: [
          if (expanded) _LauncherRail(dest: _dest, onChanged: _selectDest),
          Expanded(
            child: SafeArea(
              child: _LauncherContent(
                dest: _dest,
                expanded: expanded,
                storageGranted: _storageGranted,
                loading: _loading,
                scanStatus: _scanStatus,
                games: _games,
                selectedGame: _selectedGame,
                onRequestPermission: _requestPermission,
                onEditRoot: _showRootConfig,
                onScan: _scanGames,
                onSelectGame: (game) => setState(() => _selectedGame = game),
                onUpdateGame: _updateGame,
                onLaunch: _launch,
                onOpenSettings: _openSettings,
                onOpenDiagnostics: _openDiagnostics,
              ),
            ),
          ),
        ],
      ),
      bottomNavigationBar: expanded
          ? null
          : NavigationBar(
              selectedIndex: _dest.index,
              onDestinationSelected: (index) => _selectDest(_LauncherDest.values[index]),
              destinations: const [
                NavigationDestination(icon: Icon(Icons.home_outlined), selectedIcon: Icon(Icons.home), label: '游戏库'),
                NavigationDestination(icon: Icon(Icons.settings_outlined), selectedIcon: Icon(Icons.settings), label: '设置'),
                NavigationDestination(icon: Icon(Icons.info_outline), selectedIcon: Icon(Icons.info), label: '工具'),
              ],
            ),
    );
  }
}

class _LauncherContent extends StatelessWidget {
  const _LauncherContent({
    required this.dest,
    required this.expanded,
    required this.storageGranted,
    required this.loading,
    required this.scanStatus,
    required this.games,
    required this.selectedGame,
    required this.onRequestPermission,
    required this.onEditRoot,
    required this.onScan,
    required this.onSelectGame,
    required this.onUpdateGame,
    required this.onLaunch,
    required this.onOpenSettings,
    required this.onOpenDiagnostics,
  });

  final _LauncherDest dest;
  final bool expanded;
  final bool storageGranted;
  final bool loading;
  final String scanStatus;
  final List<GameEntry> games;
  final GameEntry? selectedGame;
  final VoidCallback onRequestPermission;
  final VoidCallback onEditRoot;
  final VoidCallback onScan;
  final ValueChanged<GameEntry> onSelectGame;
  final ValueChanged<GameEntry> onUpdateGame;
  final ValueChanged<GameEntry> onLaunch;
  final VoidCallback onOpenSettings;
  final VoidCallback onOpenDiagnostics;

  @override
  Widget build(BuildContext context) {
    return CustomScrollView(
      slivers: [
        SliverAppBar.large(
          title: const Text('KiriKiri Launcher'),
          actions: [
            IconButton(onPressed: onEditRoot, icon: const Icon(Icons.add), tooltip: '游戏根目录'),
            IconButton(onPressed: onScan, icon: const Icon(Icons.refresh), tooltip: '重新扫描'),
          ],
        ),
        SliverPadding(
          padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
          sliver: SliverList(
            delegate: SliverChildListDelegate([
              _LauncherHero(
                storageGranted: storageGranted,
                gameCount: games.length,
                loading: loading,
                scanStatus: scanStatus,
                onRequestPermission: onRequestPermission,
                onScan: onScan,
              ),
              const SizedBox(height: 14),
              if (dest == _LauncherDest.library)
                _LibraryPanel(
                  expanded: expanded,
                  loading: loading,
                  games: games,
                  selectedGame: selectedGame,
                  onSelectGame: onSelectGame,
                  onUpdateGame: onUpdateGame,
                  onLaunch: onLaunch,
                )
              else if (dest == _LauncherDest.settings)
                _SettingsPanel(
                  storageGranted: storageGranted,
                  onRequestPermission: onRequestPermission,
                  onOpenSettings: onOpenSettings,
                  onOpenDiagnostics: onOpenDiagnostics,
                )
              else
                _ToolsPanel(onOpenSettings: onOpenSettings, onOpenDiagnostics: onOpenDiagnostics),
            ]),
          ),
        ),
      ],
    );
  }
}

class _LauncherRail extends StatelessWidget {
  const _LauncherRail({required this.dest, required this.onChanged});

  final _LauncherDest dest;
  final ValueChanged<_LauncherDest> onChanged;

  @override
  Widget build(BuildContext context) {
    return NavigationRail(
      selectedIndex: dest.index,
      onDestinationSelected: (index) => onChanged(_LauncherDest.values[index]),
      labelType: NavigationRailLabelType.all,
      destinations: const [
        NavigationRailDestination(icon: Icon(Icons.home_outlined), selectedIcon: Icon(Icons.home), label: Text('游戏库')),
        NavigationRailDestination(icon: Icon(Icons.settings_outlined), selectedIcon: Icon(Icons.settings), label: Text('设置')),
        NavigationRailDestination(icon: Icon(Icons.info_outline), selectedIcon: Icon(Icons.info), label: Text('工具')),
      ],
    );
  }
}

class _LauncherHero extends StatelessWidget {
  const _LauncherHero({
    required this.storageGranted,
    required this.gameCount,
    required this.loading,
    required this.scanStatus,
    required this.onRequestPermission,
    required this.onScan,
  });

  final bool storageGranted;
  final int gameCount;
  final bool loading;
  final String scanStatus;
  final VoidCallback onRequestPermission;
  final VoidCallback onScan;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Card(
      clipBehavior: Clip.antiAlias,
      child: Container(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            colors: [scheme.primaryContainer, scheme.secondaryContainer, scheme.surfaceContainerHighest],
          ),
        ),
        padding: const EdgeInsets.all(20),
        child: Wrap(
          runSpacing: 16,
          spacing: 16,
          crossAxisAlignment: WrapCrossAlignment.center,
          alignment: WrapAlignment.spaceBetween,
          children: [
            ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 560),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      CircleAvatar(backgroundColor: scheme.primary, child: const ResourceIcon('menu_icon.png')),
                      const SizedBox(width: 12),
                      Text('KrKr2 启动器', style: Theme.of(context).textTheme.headlineSmall?.copyWith(fontWeight: FontWeight.w700)),
                    ],
                  ),
                  const SizedBox(height: 10),
                  Text('选择游戏目录，扫描 KiriKiri 游戏，然后启动。', style: Theme.of(context).textTheme.bodyMedium),
                  const SizedBox(height: 12),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      Chip(avatar: Icon(storageGranted ? Icons.verified : Icons.warning_amber, size: 18), label: Text(storageGranted ? '文件管理权限已授权' : '需要文件管理权限')),
                      Chip(avatar: const Icon(Icons.grid_view, size: 18), label: Text('$gameCount 个游戏')),
                      Chip(avatar: loading ? const SizedBox.square(dimension: 16, child: CircularProgressIndicator(strokeWidth: 2)) : const Icon(Icons.task_alt, size: 18), label: Text(scanStatus)),
                    ],
                  ),
                ],
              ),
            ),
            Wrap(
              spacing: 10,
              runSpacing: 10,
              children: [
                FilledButton.icon(onPressed: onRequestPermission, icon: const Icon(Icons.folder_special), label: const Text('申请文件管理权限')),
                OutlinedButton.icon(onPressed: onScan, icon: const Icon(Icons.refresh), label: const Text('重新扫描')),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _RootConfigCard extends StatelessWidget {
  const _RootConfigCard({required this.controller, required this.onPickRoot, required this.onScan});

  final TextEditingController controller;
  final VoidCallback onPickRoot;
  final VoidCallback onScan;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('游戏根目录', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 10),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: controller,
                    decoration: const InputDecoration(prefixIcon: Icon(Icons.folder), border: OutlineInputBorder(), hintText: '/storage/emulated/0/krkr2pro'),
                    onSubmitted: (_) => onScan(),
                  ),
                ),
                const SizedBox(width: 10),
                IconButton(onPressed: onPickRoot, icon: const Icon(Icons.folder_open), tooltip: '选择'),
                const SizedBox(width: 6),
                IconButton.filled(onPressed: onScan, icon: const Icon(Icons.search), tooltip: '扫描'),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _LibraryPanel extends StatelessWidget {
  const _LibraryPanel({
    required this.expanded,
    required this.loading,
    required this.games,
    required this.selectedGame,
    required this.onSelectGame,
    required this.onUpdateGame,
    required this.onLaunch,
  });

  final bool expanded;
  final bool loading;
  final List<GameEntry> games;
  final GameEntry? selectedGame;
  final ValueChanged<GameEntry> onSelectGame;
  final ValueChanged<GameEntry> onUpdateGame;
  final ValueChanged<GameEntry> onLaunch;

  @override
  Widget build(BuildContext context) {
    if (loading) {
      return const SizedBox(height: 220, child: Center(child: CircularProgressIndicator()));
    }
    if (games.isEmpty) {
      return const _EmptyState();
    }
    if (expanded) {
      return SizedBox(
        height: 430,
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Expanded(flex: 3, child: _GameGrid(games: games, selectedGame: selectedGame, onSelectGame: onSelectGame, onLaunch: onLaunch)),
            const SizedBox(width: 14),
            Expanded(flex: 2, child: _GameDetailPane(game: selectedGame ?? games.first, onUpdateGame: onUpdateGame, onLaunch: onLaunch)),
          ],
        ),
      );
    }
    return Column(
      children: [
        _GameGrid(games: games, selectedGame: selectedGame, onSelectGame: onSelectGame, onLaunch: onLaunch, shrinkWrap: true),
        const SizedBox(height: 14),
        if (selectedGame != null) _GameDetailPane(game: selectedGame!, onUpdateGame: onUpdateGame, onLaunch: onLaunch),
      ],
    );
  }
}

class _GameGrid extends StatelessWidget {
  const _GameGrid({
    required this.games,
    required this.selectedGame,
    required this.onSelectGame,
    required this.onLaunch,
    this.shrinkWrap = false,
  });

  final List<GameEntry> games;
  final GameEntry? selectedGame;
  final ValueChanged<GameEntry> onSelectGame;
  final ValueChanged<GameEntry> onLaunch;
  final bool shrinkWrap;

  @override
  Widget build(BuildContext context) {
    return GridView.builder(
      shrinkWrap: shrinkWrap,
      physics: shrinkWrap ? const NeverScrollableScrollPhysics() : null,
      gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(maxCrossAxisExtent: 280, mainAxisSpacing: 12, crossAxisSpacing: 12, childAspectRatio: 1.35),
      itemCount: games.length,
      itemBuilder: (context, index) {
        final game = games[index];
        return _GameCard(
          game: game,
          selected: selectedGame?.path == game.path,
          onTap: () => onSelectGame(game),
          onLaunch: () => onLaunch(game),
        );
      },
    );
  }
}

class _GameCard extends StatelessWidget {
  const _GameCard({required this.game, required this.selected, required this.onTap, required this.onLaunch});

  final GameEntry game;
  final bool selected;
  final VoidCallback onTap;
  final VoidCallback onLaunch;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Card(
      clipBehavior: Clip.antiAlias,
      color: selected ? scheme.primaryContainer : null,
      child: InkWell(
        onTap: onTap,
        onDoubleTap: onLaunch,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  _CoverAvatar(path: game.coverPath),
                  const Spacer(),
                  IconButton(onPressed: onLaunch, icon: const Icon(Icons.play_arrow), tooltip: '启动'),
                ],
              ),
              const Spacer(),
              Text(game.title, maxLines: 2, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700)),
              const SizedBox(height: 4),
              Text(game.subtitle ?? game.path, maxLines: 2, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.bodySmall?.copyWith(color: scheme.onSurfaceVariant)),
            ],
          ),
        ),
      ),
    );
  }
}

class _CoverAvatar extends StatelessWidget {
  const _CoverAvatar({required this.path});

  final String? path;

  @override
  Widget build(BuildContext context) {
    final image = _fileImage(path);
    return CircleAvatar(
      backgroundColor: Theme.of(context).colorScheme.surface,
      backgroundImage: image,
      child: image == null ? const ResourceIcon('windows_icon.png') : null,
    );
  }
}

class _GameDetailPane extends StatelessWidget {
  const _GameDetailPane({required this.game, required this.onUpdateGame, required this.onLaunch});

  final GameEntry game;
  final ValueChanged<GameEntry> onUpdateGame;
  final ValueChanged<GameEntry> onLaunch;

  @override
  Widget build(BuildContext context) {
    return Card(
      clipBehavior: Clip.antiAlias,
      child: SingleChildScrollView(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            _GameBanner(game: game),
            Padding(
              padding: const EdgeInsets.all(18),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const ResourceIcon('menu_icon.png'),
                      const SizedBox(width: 10),
                      Expanded(child: Text(game.title, style: Theme.of(context).textTheme.headlineSmall)),
                    ],
                  ),
                  const SizedBox(height: 10),
                  Text(game.path, style: Theme.of(context).textTheme.bodySmall),
                  const SizedBox(height: 14),
                  _LaunchFilePicker(game: game, onChanged: onUpdateGame),
                  const SizedBox(height: 18),
                  SizedBox(
                    width: double.infinity,
                    child: FilledButton.icon(onPressed: () => onLaunch(game), icon: const Icon(Icons.play_arrow), label: const Text('启动')),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _GameBanner extends StatelessWidget {
  const _GameBanner({required this.game});

  final GameEntry game;

  @override
  Widget build(BuildContext context) {
    final imagePath = game.backgroundPath ?? game.coverPath;
    final image = _fileImage(imagePath);
    return SizedBox(
      height: 150,
      child: Stack(
        fit: StackFit.expand,
        children: [
          if (image != null)
            Image(image: image, fit: BoxFit.cover)
          else
            Container(
              decoration: BoxDecoration(
                gradient: LinearGradient(colors: [Theme.of(context).colorScheme.primaryContainer, Theme.of(context).colorScheme.surfaceContainerHighest]),
              ),
            ),
          DecoratedBox(decoration: BoxDecoration(color: Colors.black.withOpacity(0.22))),
        ],
      ),
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
          decoration: const InputDecoration(labelText: '启动文件', helperText: '自动检测会按 startup.tjs / start.tjs / data.xp3 优先级启动', border: OutlineInputBorder()),
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

class _EmptyState extends StatelessWidget {
  const _EmptyState();

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(28),
        child: Center(
          child: Column(
            children: [
              const ResourceIcon('empty.png', size: 54),
              const SizedBox(height: 12),
              Text('没有找到游戏', style: Theme.of(context).textTheme.titleLarge),
              const SizedBox(height: 8),
              const Text('请先授予文件管理权限，然后选择包含 data.xp3 或 startup.tjs 的根目录。', textAlign: TextAlign.center),
            ],
          ),
        ),
      ),
    );
  }
}

class _SettingsPanel extends StatelessWidget {
  const _SettingsPanel({required this.storageGranted, required this.onRequestPermission, required this.onOpenSettings, required this.onOpenDiagnostics});

  final bool storageGranted;
  final VoidCallback onRequestPermission;
  final VoidCallback onOpenSettings;
  final VoidCallback onOpenDiagnostics;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        _SettingsTile(icon: Icons.folder_special, title: '文件管理权限', subtitle: storageGranted ? '已授权' : '未授权，Android 11+ 需要所有文件访问权限', trailing: FilledButton(onPressed: onRequestPermission, child: const Text('申请'))),
        _SettingsTile(icon: Icons.tune, title: '设置', subtitle: '打开旧版启动器设置', trailing: OutlinedButton(onPressed: onOpenSettings, child: const Text('打开'))),
        _SettingsTile(icon: Icons.bug_report_outlined, title: '诊断', subtitle: '打开旧版诊断页', trailing: OutlinedButton(onPressed: onOpenDiagnostics, child: const Text('打开'))),
      ],
    );
  }
}

class _ToolsPanel extends StatelessWidget {
  const _ToolsPanel({required this.onOpenSettings, required this.onOpenDiagnostics});

  final VoidCallback onOpenSettings;
  final VoidCallback onOpenDiagnostics;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        _ToolTile(icon: const ResourceIcon('touch_icon.png'), title: '触摸模式', subtitle: '保留旧 UI 触控配置入口，等待 C 配置接口接入'),
        _ToolTile(icon: const ResourceIcon('mouse_icon.png'), title: '鼠标模式', subtitle: '保留左/右键与模拟鼠标入口'),
        _ToolTile(icon: const ResourceIcon('keyboard_icon.png'), title: '键盘映射', subtitle: '后续迁移快捷键和虚拟键盘配置'),
        _ToolTile(icon: const ResourceIcon('about_icon.png'), title: '关于与诊断', subtitle: '打开日志、设备和运行库信息', action: OutlinedButton(onPressed: onOpenDiagnostics, child: const Text('诊断'))),
        _ToolTile(icon: const ResourceIcon('syssetting_btn_on.png'), title: '系统设置', subtitle: '旧版设置按钮的 Flutter 入口', action: OutlinedButton(onPressed: onOpenSettings, child: const Text('打开'))),
      ],
    );
  }
}

class _SettingsTile extends StatelessWidget {
  const _SettingsTile({required this.icon, required this.title, required this.subtitle, required this.trailing});

  final IconData icon;
  final String title;
  final String subtitle;
  final Widget trailing;

  @override
  Widget build(BuildContext context) {
    return Card(child: ListTile(leading: Icon(icon), title: Text(title), subtitle: Text(subtitle), trailing: trailing));
  }
}

class _ToolTile extends StatelessWidget {
  const _ToolTile({required this.icon, required this.title, required this.subtitle, this.action});

  final Widget icon;
  final String title;
  final String subtitle;
  final Widget? action;

  @override
  Widget build(BuildContext context) {
    return Card(child: ListTile(leading: icon, title: Text(title), subtitle: Text(subtitle), trailing: action));
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
