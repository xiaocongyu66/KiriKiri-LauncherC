import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

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
  late Future<List<GameEntry>> _gamesFuture;
  int _selectedIndex = 0;

  @override
  void initState() {
    super.initState();
    _gamesFuture = _loadGames();
  }

  Future<List<GameEntry>> _loadGames() async {
    try {
      final games = await widget.bridge.scanGames();
      if (games.isNotEmpty) {
        return games;
      }
    } catch (_) {
      // Desktop/web preview without native bridge.
    }
    return const [
      GameEntry(
        title: 'Select a KiriKiri game',
        subtitle: 'Pick a folder or data.xp3 to start',
        path: '',
      ),
    ];
  }

  void _refresh() {
    setState(() => _gamesFuture = _loadGames());
  }

  Future<void> _pickGame() async {
    try {
      await widget.bridge.pickGame();
    } on MissingPluginException {
      await FilePicker.platform.getDirectoryPath(dialogTitle: 'Select game folder');
    }
    _refresh();
  }

  Future<void> _launch(GameEntry game) async {
    if (game.path.isEmpty) {
      await _pickGame();
      return;
    }
    try {
      await widget.bridge.launchGame(game);
    } catch (_) {
      if (!mounted) {
        return;
      }
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Native launcher bridge is not connected: ${game.path}')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final isWide = MediaQuery.sizeOf(context).width >= 760;
    return Scaffold(
      body: Row(
        children: [
          if (isWide) _LauncherRail(selectedIndex: _selectedIndex, onChanged: _selectTab),
          Expanded(
            child: SafeArea(
              child: IndexedStack(
                index: _selectedIndex,
                children: [
                  _HomePanel(gamesFuture: _gamesFuture, onRefresh: _refresh, onPickGame: _pickGame, onLaunch: _launch),
                  _ToolsPanel(bridge: widget.bridge),
                  _AboutPanel(onOpenDiagnostics: _openDiagnostics),
                ],
              ),
            ),
          ),
        ],
      ),
      bottomNavigationBar: isWide
          ? null
          : NavigationBar(
              selectedIndex: _selectedIndex,
              onDestinationSelected: _selectTab,
              destinations: const [
                NavigationDestination(icon: Icon(Icons.home_outlined), selectedIcon: Icon(Icons.home), label: 'Games'),
                NavigationDestination(icon: Icon(Icons.tune), label: 'Tools'),
                NavigationDestination(icon: Icon(Icons.info_outline), label: 'About'),
              ],
            ),
    );
  }

  void _selectTab(int index) {
    setState(() => _selectedIndex = index);
  }

  Future<void> _openDiagnostics() async {
    try {
      await widget.bridge.openDiagnostics();
    } catch (_) {
      if (!mounted) {
        return;
      }
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Diagnostics bridge is not connected')));
    }
  }
}

class _LauncherRail extends StatelessWidget {
  const _LauncherRail({required this.selectedIndex, required this.onChanged});

  final int selectedIndex;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return NavigationRail(
      selectedIndex: selectedIndex,
      onDestinationSelected: onChanged,
      labelType: NavigationRailLabelType.all,
      destinations: const [
        NavigationRailDestination(icon: Icon(Icons.home_outlined), selectedIcon: Icon(Icons.home), label: Text('Games')),
        NavigationRailDestination(icon: Icon(Icons.tune), label: Text('Tools')),
        NavigationRailDestination(icon: Icon(Icons.info_outline), label: Text('About')),
      ],
    );
  }
}

class _HomePanel extends StatelessWidget {
  const _HomePanel({
    required this.gamesFuture,
    required this.onRefresh,
    required this.onPickGame,
    required this.onLaunch,
  });

  final Future<List<GameEntry>> gamesFuture;
  final VoidCallback onRefresh;
  final VoidCallback onPickGame;
  final ValueChanged<GameEntry> onLaunch;

  @override
  Widget build(BuildContext context) {
    return CustomScrollView(
      slivers: [
        SliverAppBar.large(
          title: const Text('KiriKiri Launcher'),
          actions: [
            IconButton(onPressed: onRefresh, icon: const Icon(Icons.refresh)),
            IconButton(onPressed: onPickGame, icon: const Icon(Icons.create_new_folder_outlined)),
          ],
        ),
        SliverPadding(
          padding: const EdgeInsets.all(20),
          sliver: FutureBuilder<List<GameEntry>>(
            future: gamesFuture,
            builder: (context, snapshot) {
              if (snapshot.connectionState != ConnectionState.done) {
                return const SliverFillRemaining(child: Center(child: CircularProgressIndicator()));
              }
              final games = snapshot.data ?? const [];
              return SliverGrid.builder(
                gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
                  maxCrossAxisExtent: 360,
                  mainAxisSpacing: 16,
                  crossAxisSpacing: 16,
                  childAspectRatio: 1.45,
                ),
                itemCount: games.length,
                itemBuilder: (context, index) => _GameCard(game: games[index], onTap: () => onLaunch(games[index])),
              );
            },
          ),
        ),
      ],
    );
  }
}

class _GameCard extends StatelessWidget {
  const _GameCard({required this.game, required this.onTap});

  final GameEntry game;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Card(
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  CircleAvatar(
                    backgroundColor: scheme.primaryContainer,
                    child: const ResourceIcon('menu_icon.png'),
                  ),
                  const Spacer(),
                  FilledButton.icon(onPressed: onTap, icon: const Icon(Icons.play_arrow), label: const Text('Start')),
                ],
              ),
              const Spacer(),
              Text(game.title, maxLines: 2, overflow: TextOverflow.ellipsis, style: Theme.of(context).textTheme.titleLarge),
              const SizedBox(height: 6),
              Text(
                game.subtitle ?? game.path,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(color: scheme.onSurfaceVariant),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _ToolsPanel extends StatelessWidget {
  const _ToolsPanel({required this.bridge});

  final LauncherBridge bridge;

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(20),
      children: [
        Text('Runtime Tools', style: Theme.of(context).textTheme.headlineMedium),
        const SizedBox(height: 20),
        _ToolTile(icon: const ResourceIcon('touch_icon.png'), title: 'Touch mode', subtitle: 'Configure touch mapping after native bridge is attached'),
        _ToolTile(icon: const ResourceIcon('mouse_icon.png'), title: 'Mouse mode', subtitle: 'Shared resource from ui/cocos-studio/img'),
        _ToolTile(icon: const ResourceIcon('keyboard_icon.png'), title: 'Keyboard', subtitle: 'Prepared for cross-platform shortcut controls'),
        const SizedBox(height: 12),
        FilledButton.icon(
          onPressed: () async {
            try {
              await bridge.openSettings();
            } catch (_) {
              if (context.mounted) {
                ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Settings bridge is not connected')));
              }
            }
          },
          icon: const Icon(Icons.settings),
          label: const Text('Open native settings'),
        ),
      ],
    );
  }
}

class _ToolTile extends StatelessWidget {
  const _ToolTile({required this.icon, required this.title, required this.subtitle});

  final Widget icon;
  final String title;
  final String subtitle;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: ListTile(leading: icon, title: Text(title), subtitle: Text(subtitle)),
    );
  }
}

class _AboutPanel extends StatelessWidget {
  const _AboutPanel({required this.onOpenDiagnostics});

  final VoidCallback onOpenDiagnostics;

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(20),
      children: [
        Text('Modern Flutter UI', style: Theme.of(context).textTheme.headlineMedium),
        const SizedBox(height: 12),
        const Text('This launcher is the new Flutter entry point. The existing Cocos/TJS engine remains available as the runtime layer while the UI migrates incrementally.'),
        const SizedBox(height: 20),
        FilledButton.icon(onPressed: onOpenDiagnostics, icon: const Icon(Icons.bug_report_outlined), label: const Text('Diagnostics')),
      ],
    );
  }
}
