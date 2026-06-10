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

  @override
  void initState() {
    super.initState();
    _channel.setMethodCallHandler(_handleHostCall);
  }

  @override
  void dispose() {
    _channel.setMethodCallHandler(null);
    super.dispose();
  }

  Future<void> _handleHostCall(MethodCall call) async {
    if (call.method == 'showMenu') {
      setState(() {
        _expanded = true;
        _menuMode = true;
        _menuFuture = widget.bridge.getMainMenu();
      });
    }
  }

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
          Align(
            alignment: Alignment.bottomRight,
            child: _menuMode
                ? _FloatingMenuPanel(
                    future: _menuFuture,
                    onBack: () => setState(() => _menuFuture = widget.bridge.getMainMenu()),
                    onClose: () => _setExpanded(false),
                    onActivate: _activateMenu,
                  )
                : _FloatingActionTray(
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

class _FloatingActionTray extends StatelessWidget {
  const _FloatingActionTray({
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
    return GestureDetector(
      behavior: HitTestBehavior.translucent,
      onPanUpdate: expanded ? null : onDrag,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 140),
        curve: Curves.easeOutCubic,
        height: 56,
        decoration: BoxDecoration(
          color: const Color(0xd90b0b0d),
          borderRadius: BorderRadius.circular(28),
          border: Border.all(color: Colors.white.withOpacity(0.10)),
          boxShadow: const [
            BoxShadow(color: Color(0x66000000), blurRadius: 14, offset: Offset(0, 6)),
          ],
        ),
        child: ClipRRect(
          borderRadius: BorderRadius.circular(28),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (expanded) ...[
                const SizedBox(width: 4),
                _TrayButton(asset: 'menu_icon.png', label: '菜单', onTap: () => onAction('game-menu')),
                _TrayButton(asset: 'windows_icon.png', label: '窗口', onTap: () => onAction('window-manager')),
                _TrayButton(asset: mouseMode ? 'mouse_icon.png' : 'touch_icon.png', label: mouseMode ? '鼠标' : '触摸', onTap: () => onAction('mouse-mode')),
                _TrayButton(asset: 'exit_icon.png', label: '退出', destructive: true, onTap: () => onAction('exit')),
                const SizedBox(width: 2),
                Container(width: 1, height: 28, color: Colors.white.withOpacity(0.12)),
              ],
              _FloatingHandle(onTap: onToggle),
            ],
          ),
        ),
      ),
    );
  }
}

class _FloatingHandle extends StatelessWidget {
  const _FloatingHandle({required this.onTap});

  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(28),
      child: Container(
        width: 52,
        height: 56,
        alignment: Alignment.center,
        child: const Opacity(
          opacity: 0.82,
          child: ResourceIcon('menu_handler.png', size: 40),
        ),
      ),
    );
  }
}

class _TrayButton extends StatelessWidget {
  const _TrayButton({required this.asset, required this.label, required this.onTap, this.destructive = false});

  final String asset;
  final String label;
  final VoidCallback onTap;
  final bool destructive;

  @override
  Widget build(BuildContext context) {
    final foreground = destructive ? const Color(0xffffb4ab) : Colors.white.withOpacity(0.82);
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(18),
      child: SizedBox(
        width: 52,
        height: 56,
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            ResourceIcon(asset, size: 24),
            const SizedBox(height: 2),
            Text(
              label,
              maxLines: 1,
              overflow: TextOverflow.fade,
              softWrap: false,
              style: Theme.of(context).textTheme.labelSmall?.copyWith(color: foreground, fontSize: 10),
            ),
          ],
        ),
      ),
    );
  }
}

class _FloatingMenuPanel extends StatelessWidget {
  const _FloatingMenuPanel({required this.future, required this.onBack, required this.onClose, required this.onActivate});

  final Future<List<GameMenuItem>> future;
  final VoidCallback onBack;
  final VoidCallback onClose;
  final ValueChanged<GameMenuItem> onActivate;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(6),
      child: ClipRRect(
        borderRadius: BorderRadius.circular(18),
        child: DecoratedBox(
          decoration: BoxDecoration(
            color: const Color(0xe60b0b0d),
            border: Border.all(color: Colors.white.withOpacity(0.10)),
            boxShadow: const [
              BoxShadow(color: Color(0x66000000), blurRadius: 18, offset: Offset(0, 8)),
            ],
          ),
          child: Column(
            children: [
              SizedBox(
                height: 46,
                child: Row(
                  children: [
                    IconButton(
                      visualDensity: VisualDensity.compact,
                      onPressed: onBack,
                      icon: const Icon(Icons.arrow_back_rounded, color: Colors.white70),
                      tooltip: '返回',
                    ),
                    const ResourceIcon('menu_icon.png', size: 24),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Text(
                        '游戏菜单',
                        style: Theme.of(context).textTheme.titleSmall?.copyWith(color: Colors.white, fontWeight: FontWeight.w700),
                      ),
                    ),
                    IconButton(
                      visualDensity: VisualDensity.compact,
                      onPressed: onClose,
                      icon: const Icon(Icons.close_rounded, color: Colors.white70),
                      tooltip: '收起',
                    ),
                  ],
                ),
              ),
              Divider(height: 1, color: Colors.white.withOpacity(0.12)),
              Expanded(
                child: FutureBuilder<List<GameMenuItem>>(
                  future: future,
                  builder: (context, snapshot) {
                    final items = snapshot.data ?? const <GameMenuItem>[];
                    if (snapshot.connectionState == ConnectionState.waiting) {
                      return const Center(child: SizedBox(width: 22, height: 22, child: CircularProgressIndicator(strokeWidth: 2)));
                    }
                    if (items.isEmpty) {
                      return const Center(child: Text('当前游戏没有菜单', style: TextStyle(color: Colors.white70)));
                    }
                    return Scrollbar(
                      child: ListView.separated(
                        padding: const EdgeInsets.symmetric(vertical: 4),
                        itemCount: items.length,
                        separatorBuilder: (_, __) => Divider(height: 1, indent: 48, color: Colors.white.withOpacity(0.08)),
                        itemBuilder: (context, index) => _FloatingMenuTile(item: items[index], onTap: onActivate),
                      ),
                    );
                  },
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _FloatingMenuTile extends StatelessWidget {
  const _FloatingMenuTile({required this.item, required this.onTap});

  final GameMenuItem item;
  final ValueChanged<GameMenuItem> onTap;

  @override
  Widget build(BuildContext context) {
    if (item.separator) {
      return Padding(
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
        child: Divider(height: 1, thickness: 1, color: Colors.white.withOpacity(0.16)),
      );
    }
    final hasChildren = item.children.isNotEmpty;
    final enabledColor = item.enabled ? Colors.white.withOpacity(0.84) : Colors.white.withOpacity(0.32);
    return InkWell(
      onTap: item.enabled ? () => onTap(item) : null,
      child: SizedBox(
        height: 42,
        child: Row(
          children: [
            const SizedBox(width: 12),
            Icon(
              hasChildren ? Icons.folder_open_rounded : item.checked ? Icons.check_rounded : Icons.radio_button_unchecked_rounded,
              color: enabledColor,
              size: 19,
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Text(
                item.title.isEmpty ? '(未命名)' : item.title,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(color: enabledColor),
              ),
            ),
            if (hasChildren) const Icon(Icons.chevron_right_rounded, color: Colors.white70, size: 20),
            const SizedBox(width: 10),
          ],
        ),
      ),
    );
  }
}
