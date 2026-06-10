import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/launcher_bridge.dart';
import '../models/game_menu_item.dart';
import '../widgets/resource_icon.dart';

class GameOverlayPage extends StatefulWidget {
  const GameOverlayPage({required this.bridge, super.key});

  final LauncherBridge bridge;

  @override
  State<GameOverlayPage> createState() => _GameOverlayPageState();
}

class _GameOverlayPageState extends State<GameOverlayPage> {
  static const MethodChannel _channel = MethodChannel('org.github.krkr2/game_overlay');

  bool _expanded = false;
  bool _menuMode = false;
  bool _mouseMode = true;
  late Future<List<GameMenuItem>> _menuFuture = widget.bridge.getMainMenu();

  Future<void> _setExpanded(bool value, {bool menuMode = false}) async {
    setState(() {
      _expanded = value;
      _menuMode = menuMode;
      if (menuMode) {
        _menuFuture = widget.bridge.getMainMenu();
      }
    });
    await _invokeHost('setExpanded', {'expanded': value, 'menuMode': menuMode});
  }

  Future<void> _move(DragUpdateDetails details) async {
    if (_expanded) {
      return;
    }
    await _invokeHost('move', {'dx': details.delta.dx, 'dy': details.delta.dy});
  }

  Future<void> _invokeHost(String method, Map<String, Object?> arguments) async {
    try {
      await _channel.invokeMethod<void>(method, arguments);
    } on MissingPluginException {
      // iOS/desktop hosts can show the same Flutter overlay route without a
      // resize channel until their native overlay container is wired in.
    }
  }

  Future<void> _runAction(String action) async {
    if (action == 'game-menu') {
      await _setExpanded(true, menuMode: true);
      return;
    }
    try {
      final result = await widget.bridge.performOverlayAction(action);
      if (action == 'mouse-mode') {
        setState(() => _mouseMode = result != 0);
      }
      await _setExpanded(false);
    } catch (error) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('执行失败：$error')));
      }
    }
  }

  Future<void> _activateMenu(GameMenuItem item) async {
    if (!item.enabled) {
      return;
    }
    if (item.children.isNotEmpty) {
      setState(() => _menuFuture = Future.value(item.children));
      return;
    }
    try {
      await widget.bridge.activateMenuItem(item);
      await _setExpanded(false);
    } catch (error) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('菜单执行失败：$error')));
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Material(
      type: MaterialType.transparency,
      child: Stack(
        children: [
          if (_expanded)
            Positioned.fill(
              child: GestureDetector(
                behavior: HitTestBehavior.opaque,
                onTap: () => _setExpanded(false),
                child: const SizedBox.expand(),
              ),
            ),
          Align(
            alignment: Alignment.bottomCenter,
            child: _menuMode
                ? _LegacyMenuPanel(
                    future: _menuFuture,
                    onBack: () => setState(() => _menuFuture = widget.bridge.getMainMenu()),
                    onClose: () => _setExpanded(false),
                    onActivate: _activateMenu,
                  )
                : _LegacyActionBar(
                    expanded: _expanded,
                    mouseMode: _mouseMode,
                    onDrag: _move,
                    onToggle: () => _setExpanded(!_expanded),
                    onAction: _runAction,
                  ),
          ),
        ],
      ),
    );
  }
}

class _LegacyActionBar extends StatelessWidget {
  const _LegacyActionBar({
    required this.expanded,
    required this.mouseMode,
    required this.onDrag,
    required this.onToggle,
    required this.onAction,
  });

  final bool expanded;
  final bool mouseMode;
  final GestureDragUpdateCallback onDrag;
  final VoidCallback onToggle;
  final ValueChanged<String> onAction;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: expanded ? 178 : 56,
      child: Stack(
        children: [
          if (expanded)
            Positioned(
              left: 0,
              right: 0,
              bottom: 0,
              height: 130,
              child: DecoratedBox(
                decoration: BoxDecoration(color: const Color(0xff2a2a2a).withOpacity(0.94)),
                child: Row(
                  children: [
                    _LegacyButton(asset: 'menu_icon.png', label: '菜单', onTap: () => onAction('game-menu')),
                    _LegacyButton(asset: 'windows_icon.png', label: '窗口', onTap: () => onAction('window-manager')),
                    _LegacyButton(asset: mouseMode ? 'mouse_icon.png' : 'touch_icon.png', label: mouseMode ? '鼠标' : '触摸', onTap: () => onAction('mouse-mode')),
                    _LegacyButton(asset: 'keyboard_icon.png', label: '键盘', onTap: () => onAction('keyboard')),
                    _LegacyButton(asset: 'exit_icon.png', label: '退出', onTap: () => onAction('exit')),
                  ],
                ),
              ),
            ),
          Positioned(
            right: 0,
            bottom: expanded ? 130 : 0,
            child: GestureDetector(
              onPanUpdate: onDrag,
              child: _LegacyHandle(onTap: onToggle),
            ),
          ),
        ],
      ),
    );
  }
}

class _LegacyHandle extends StatelessWidget {
  const _LegacyHandle({required this.onTap});

  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Opacity(
        opacity: 0.72,
        child: Container(
          width: 56,
          height: 56,
          alignment: Alignment.center,
          child: const ResourceIcon('menu_handler.png', size: 48),
        ),
      ),
    );
  }
}

class _LegacyButton extends StatelessWidget {
  const _LegacyButton({required this.asset, required this.label, required this.onTap});

  final String asset;
  final String label;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Expanded(
      child: InkWell(
        onTap: onTap,
        child: SizedBox.expand(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              ResourceIcon(asset, size: 64),
              const SizedBox(height: 8),
              Text(label, style: Theme.of(context).textTheme.labelMedium?.copyWith(color: Colors.white70)),
            ],
          ),
        ),
      ),
    );
  }
}

class _LegacyMenuPanel extends StatelessWidget {
  const _LegacyMenuPanel({required this.future, required this.onBack, required this.onClose, required this.onActivate});

  final Future<List<GameMenuItem>> future;
  final VoidCallback onBack;
  final VoidCallback onClose;
  final ValueChanged<GameMenuItem> onActivate;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 360,
      decoration: BoxDecoration(color: const Color(0xff2a2a2a).withOpacity(0.96)),
      child: Column(
        children: [
          SizedBox(
            height: 56,
            child: Row(
              children: [
                IconButton(onPressed: onBack, icon: const Icon(Icons.arrow_back_rounded, color: Colors.white70), tooltip: '返回'),
                const ResourceIcon('menu_icon.png', size: 28),
                const SizedBox(width: 8),
                Expanded(child: Text('菜单', style: Theme.of(context).textTheme.titleMedium?.copyWith(color: Colors.white, fontWeight: FontWeight.w700))),
                IconButton(onPressed: onClose, icon: const Icon(Icons.close_rounded, color: Colors.white70), tooltip: '收起'),
              ],
            ),
          ),
          const Divider(height: 1, color: Colors.white24),
          Expanded(
            child: FutureBuilder<List<GameMenuItem>>(
              future: future,
              builder: (context, snapshot) {
                final items = snapshot.data ?? const <GameMenuItem>[];
                if (snapshot.connectionState == ConnectionState.waiting) {
                  return const Center(child: CircularProgressIndicator());
                }
                if (items.isEmpty) {
                  return const Center(child: Text('当前游戏没有菜单', style: TextStyle(color: Colors.white70)));
                }
                return ListView.separated(
                  padding: const EdgeInsets.symmetric(vertical: 4),
                  itemCount: items.length,
                  separatorBuilder: (_, __) => const Divider(height: 1, color: Colors.white12),
                  itemBuilder: (context, index) => _LegacyMenuTile(item: items[index], onTap: onActivate),
                );
              },
            ),
          ),
        ],
      ),
    );
  }
}

class _LegacyMenuTile extends StatelessWidget {
  const _LegacyMenuTile({required this.item, required this.onTap});

  final GameMenuItem item;
  final ValueChanged<GameMenuItem> onTap;

  @override
  Widget build(BuildContext context) {
    final hasChildren = item.children.isNotEmpty;
    return ListTile(
      enabled: item.enabled,
      leading: Icon(hasChildren ? Icons.folder_open_rounded : item.checked ? Icons.check_rounded : Icons.circle_outlined, color: item.enabled ? Colors.white70 : Colors.white30),
      title: Text(item.title.isEmpty ? '(未命名)' : item.title, maxLines: 1, overflow: TextOverflow.ellipsis, style: TextStyle(color: item.enabled ? Colors.white : Colors.white38)),
      trailing: hasChildren ? const Icon(Icons.chevron_right_rounded, color: Colors.white70) : null,
      onTap: () => onTap(item),
    );
  }
}
