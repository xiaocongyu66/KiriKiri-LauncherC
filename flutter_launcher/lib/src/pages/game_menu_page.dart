import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/launcher_bridge.dart';
import '../models/game_menu_item.dart';
import '../widgets/resource_icon.dart';

class GameMenuPage extends StatefulWidget {
  const GameMenuPage({required this.bridge, this.embedded = false, super.key});

  final LauncherBridge bridge;
  final bool embedded;

  @override
  State<GameMenuPage> createState() => _GameMenuPageState();
}

class _GameMenuPageState extends State<GameMenuPage> {
  late Future<List<GameMenuItem>> _future = widget.bridge.getMainMenu();

  void _reload() {
    setState(() => _future = widget.bridge.getMainMenu());
  }

  Future<void> _close() async {
    if (Navigator.of(context).canPop()) {
      Navigator.of(context).pop();
      return;
    }
    await SystemNavigator.pop();
  }

  Future<void> _activate(GameMenuItem item) async {
    if (!item.enabled) {
      return;
    }
    if (item.children.isNotEmpty) {
      await showModalBottomSheet<void>(
        context: context,
        showDragHandle: true,
        builder: (context) => _MenuSheet(
          title: item.title,
          items: item.children,
          onActivate: (child) async {
            Navigator.of(context).pop();
            await _activate(child);
          },
        ),
      );
      return;
    }
    try {
      await widget.bridge.activateMenuItem(item);
      await _close();
    } catch (error) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('菜单执行失败：$error')));
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final body = FutureBuilder<List<GameMenuItem>>(
      future: _future,
      builder: (context, snapshot) {
        final items = snapshot.data ?? const <GameMenuItem>[];
        return ListView(
          padding: const EdgeInsets.all(16),
          children: [
            _MenuCard(
              child: Row(
                children: [
                  const ResourceIcon('menu_icon.png', size: 36),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text('游戏菜单', style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w700)),
                        const SizedBox(height: 4),
                        Text('Flutter TVPGameMainMenu', style: Theme.of(context).textTheme.bodySmall),
                      ],
                    ),
                  ),
                  IconButton(onPressed: _reload, tooltip: '刷新', icon: const Icon(Icons.refresh_rounded)),
                  IconButton(onPressed: _close, tooltip: '关闭', icon: const Icon(Icons.close_rounded)),
                ],
              ),
            ),
            const SizedBox(height: 12),
            _MenuCard(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      Icon(Icons.menu_open_rounded, size: 20, color: Theme.of(context).colorScheme.primary),
                      const SizedBox(width: 8),
                      Expanded(child: Text('当前菜单', style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700))),
                    ],
                  ),
                  const SizedBox(height: 8),
                  if (snapshot.connectionState == ConnectionState.waiting) const LinearProgressIndicator(),
                  if (items.isEmpty && snapshot.connectionState != ConnectionState.waiting)
                    const Padding(
                      padding: EdgeInsets.symmetric(vertical: 36),
                      child: Center(child: Text('当前游戏没有可用菜单')),
                    ),
                  for (final item in items) _MenuItemTile(item: item, depth: 0, onTap: _activate),
                ],
              ),
            ),
          ],
        );
      },
    );

    if (widget.embedded) {
      return Scaffold(
        backgroundColor: Theme.of(context).colorScheme.surfaceContainerLowest,
        body: SafeArea(child: body),
      );
    }
    return body;
  }
}

class _MenuSheet extends StatelessWidget {
  const _MenuSheet({required this.title, required this.items, required this.onActivate});

  final String title;
  final List<GameMenuItem> items;
  final ValueChanged<GameMenuItem> onActivate;

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: ListView(
        padding: const EdgeInsets.fromLTRB(16, 0, 16, 24),
        shrinkWrap: true,
        children: [
          Text(title, style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w700)),
          const SizedBox(height: 12),
          for (final item in items) _MenuItemTile(item: item, depth: 0, onTap: onActivate),
        ],
      ),
    );
  }
}

class _MenuItemTile extends StatelessWidget {
  const _MenuItemTile({required this.item, required this.depth, required this.onTap});

  final GameMenuItem item;
  final int depth;
  final ValueChanged<GameMenuItem> onTap;

  @override
  Widget build(BuildContext context) {
    final hasChildren = item.children.isNotEmpty;
    final colorScheme = Theme.of(context).colorScheme;
    return Padding(
      padding: EdgeInsets.only(left: depth * 14.0),
      child: Column(
        children: [
          ListTile(
            enabled: item.enabled,
            contentPadding: const EdgeInsets.symmetric(horizontal: 4),
            leading: Icon(
              hasChildren ? Icons.folder_open_rounded : item.checked ? Icons.check_circle_rounded : Icons.radio_button_unchecked_rounded,
              color: item.enabled ? colorScheme.primary : colorScheme.onSurfaceVariant,
            ),
            title: Text(item.title.isEmpty ? '(未命名)' : item.title, maxLines: 1, overflow: TextOverflow.ellipsis),
            subtitle: hasChildren ? Text('${item.children.length} 个子项') : null,
            trailing: hasChildren ? const Icon(Icons.chevron_right_rounded) : null,
            onTap: () => onTap(item),
          ),
          if (depth == 0) const Divider(height: 1),
        ],
      ),
    );
  }
}

class _MenuCard extends StatelessWidget {
  const _MenuCard({required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Card(child: Padding(padding: const EdgeInsets.all(16), child: child));
  }
}
