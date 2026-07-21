import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/launcher_bridge.dart';
import '../models/game_menu_item.dart';
import '../widgets/md3_components.dart';

class GameOverlayPage extends StatefulWidget {
  const GameOverlayPage({required this.bridge, super.key});

  final LauncherBridge bridge;

  @override
  State<GameOverlayPage> createState() => _GameOverlayPageState();
}

class _GameOverlayPageState extends State<GameOverlayPage> {
  static const MethodChannel _channel =
      MethodChannel('org.github.krkr2/game_overlay');
  static const int _gameBufferWidth = 1920;
  static const int _gameBufferHeight = 1080;
  static const double _gameAspectRatio = _gameBufferWidth / _gameBufferHeight;

  bool _expanded = false;
  bool _menuMode = false;
  bool _mouseMode = true;
  double _overlayRight = 18;
  double _overlayBottom = 18;
  Size _overlayBounds = Size.zero;
  Size _gameDisplaySize = Size.zero;
  final ValueNotifier<Offset> _overlayOffset =
      ValueNotifier<Offset>(const Offset(18, 18));
  _LoadingConsoleSnapshot _loadingConsole = const _LoadingConsoleSnapshot();
  _RenderOverlayStats _renderStats = const _RenderOverlayStats();
  Timer? _metricsTimer;
  final Map<int, Offset> _activePointers = <int, Offset>{};
  late Future<List<GameMenuItem>> _menuFuture = widget.bridge.getMainMenu();

  @override
  void initState() {
    super.initState();
    _channel.setMethodCallHandler(_handleHostCall);
    _metricsTimer = Timer.periodic(
      const Duration(milliseconds: 250),
      (_) {
        _refreshOverlayState();
      },
    );
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) {
        _refreshOverlayState();
      }
    });
  }

  @override
  void dispose() {
    _metricsTimer?.cancel();
    _metricsTimer = null;
    _channel.setMethodCallHandler(null);
    _overlayOffset.dispose();
    super.dispose();
  }

  Future<void> _handleHostCall(MethodCall call) async {
    if (call.method == 'showMenu') {
      setState(() {
        _expanded = true;
        _menuMode = true;
        _menuFuture = widget.bridge.getMainMenu();
      });
    } else if (call.method == 'refreshOverlayState') {
      _refreshOverlayState();
    }
  }

  void _refreshOverlayState() {
    unawaited(_refreshLoadingConsole());
    unawaited(_refreshRenderOverlayStats());
  }

  Future<void> _setExpanded(bool value, {bool menuMode = false}) async {
    setState(() {
      _expanded = value;
      _menuMode = menuMode;
      if (menuMode) {
        _menuFuture = widget.bridge.getMainMenu();
      }
    });
  }

  void _move(DragUpdateDetails details) {
    if (_expanded) {
      return;
    }
    final nextRight = (_overlayRight - details.delta.dx)
        .clamp(8.0, _maxOverlayRight)
        .toDouble();
    final nextBottom = (_overlayBottom - details.delta.dy)
        .clamp(8.0, _maxOverlayBottom)
        .toDouble();
    if (nextRight == _overlayRight && nextBottom == _overlayBottom) {
      return;
    }
    _overlayRight = nextRight;
    _overlayBottom = nextBottom;
    _publishOverlayOffset();
  }

  void _clampOverlayPosition({bool deferNotify = false}) {
    final nextRight = _overlayRight.clamp(8.0, _maxOverlayRight).toDouble();
    final nextBottom = _overlayBottom.clamp(8.0, _maxOverlayBottom).toDouble();
    if (nextRight == _overlayRight && nextBottom == _overlayBottom) {
      return;
    }
    _overlayRight = nextRight;
    _overlayBottom = nextBottom;
    _publishOverlayOffset(defer: deferNotify);
  }

  void _publishOverlayOffset({bool defer = false}) {
    final next = Offset(_overlayRight, _overlayBottom);
    if (_overlayOffset.value == next) {
      return;
    }
    if (!defer) {
      _overlayOffset.value = next;
      return;
    }
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted && _overlayOffset.value != next) {
        _overlayOffset.value = next;
      }
    });
  }

  double get _maxOverlayRight =>
      (_overlayBounds.width - 56).clamp(8.0, double.infinity).toDouble();

  double get _maxOverlayBottom =>
      (_overlayBounds.height - 56).clamp(8.0, double.infinity).toDouble();

  Future<void> _invokeHost(
      String method, Map<String, Object?> arguments) async {
    try {
      await _channel.invokeMethod<void>(method, arguments);
    } on MissingPluginException {
      // iOS/desktop hosts can show the same Flutter overlay route without a
      // resize channel until their native overlay container is wired in.
    }
  }

  Future<Map<Object?, Object?>?> _invokeHostMap(
      String method, Map<String, Object?> arguments) async {
    try {
      return await _channel.invokeMapMethod<Object?, Object?>(
          method, arguments);
    } on MissingPluginException {
      return null;
    }
  }

  Future<void> _refreshLoadingConsole() async {
    final result = await _invokeHostMap(
        'getLoadingConsoleSnapshot', const <String, Object?>{});
    if (!mounted || result == null) {
      return;
    }
    final next = _LoadingConsoleSnapshot.fromMap(result);
    if (next.active != _loadingConsole.active ||
        next.session != _loadingConsole.session ||
        next.totalLines != _loadingConsole.totalLines) {
      setState(() => _loadingConsole = next);
    }
  }

  Future<void> _refreshRenderOverlayStats() async {
    final result = await _invokeHostMap(
        'getRenderOverlayStats', const <String, Object?>{});
    if (!mounted || result == null) {
      return;
    }
    final next = _RenderOverlayStats.fromMap(result);
    if (next.showFps != _renderStats.showFps ||
        next.available != _renderStats.available ||
        next.sequence != _renderStats.sequence) {
      setState(() => _renderStats = next);
    }
  }

  Offset _toSurfacePosition(Offset localPosition) {
    final displayWidth = _gameDisplaySize.width;
    final displayHeight = _gameDisplaySize.height;
    if (displayWidth <= 0 || displayHeight <= 0) {
      return Offset.zero;
    }
    final surfaceWidth = _gameBufferWidth.toDouble();
    final surfaceHeight = _gameBufferHeight.toDouble();
    final maxX = math.max(0.0, surfaceWidth - 1.0);
    final maxY = math.max(0.0, surfaceHeight - 1.0);
    return Offset(
      (localPosition.dx * surfaceWidth / displayWidth).clamp(0.0, maxX),
      (localPosition.dy * surfaceHeight / displayHeight).clamp(0.0, maxY),
    );
  }

  Size _containedGameDisplaySize(
      double availableWidth, double availableHeight) {
    if (availableWidth <= 0 || availableHeight <= 0) {
      return Size.zero;
    }
    final widthFromHeight = availableHeight * _gameAspectRatio;
    if (widthFromHeight <= availableWidth) {
      return Size(widthFromHeight, availableHeight);
    }
    return Size(availableWidth, availableWidth / _gameAspectRatio);
  }

  Future<void> _sendPointer(String method, PointerEvent event) async {
    final position = _toSurfacePosition(event.localPosition);
    await _invokeHost(method, {
      'id': event.pointer,
      'x': position.dx,
      'y': position.dy,
    });
  }

  Future<void> _sendPointerMove() async {
    final entries = _activePointers.entries.toList(growable: false);
    await _invokeHost('gameTouchMove', {
      'ids': entries.map((entry) => entry.key).toList(growable: false),
      'xs': entries
          .map((entry) => _toSurfacePosition(entry.value).dx)
          .toList(growable: false),
      'ys': entries
          .map((entry) => _toSurfacePosition(entry.value).dy)
          .toList(growable: false),
    });
  }

  void _handlePointerDown(PointerDownEvent event) {
    _activePointers[event.pointer] = event.localPosition;
    unawaited(_sendPointer('gameTouchBegin', event));
  }

  void _handlePointerMove(PointerMoveEvent event) {
    if (!_activePointers.containsKey(event.pointer)) {
      return;
    }
    _activePointers[event.pointer] = event.localPosition;
    unawaited(_sendPointerMove());
  }

  void _handlePointerUp(PointerUpEvent event) {
    _activePointers.remove(event.pointer);
    unawaited(_sendPointer('gameTouchEnd', event));
  }

  void _handlePointerCancel(PointerCancelEvent event) {
    final position = event.localPosition;
    _activePointers[event.pointer] = position;
    final entries = _activePointers.entries.toList(growable: false);
    _activePointers.remove(event.pointer);
    unawaited(_invokeHost('gameTouchCancel', {
      'ids': entries.map((entry) => entry.key).toList(growable: false),
      'xs': entries
          .map((entry) => _toSurfacePosition(entry.value).dx)
          .toList(growable: false),
      'ys': entries
          .map((entry) => _toSurfacePosition(entry.value).dy)
          .toList(growable: false),
    }));
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
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('执行失败：$error')));
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
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('菜单执行失败：$error')));
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Material(
      type: MaterialType.transparency,
      child: LayoutBuilder(
        builder: (context, constraints) {
          _overlayBounds = Size(constraints.maxWidth, constraints.maxHeight);
          final mediaSize = MediaQuery.sizeOf(context);
          final fallbackLogicalWidth = constraints.hasBoundedWidth &&
                  constraints.maxWidth.isFinite &&
                  constraints.maxWidth > 0
              ? constraints.maxWidth
              : mediaSize.width;
          final fallbackLogicalHeight = constraints.hasBoundedHeight &&
                  constraints.maxHeight.isFinite &&
                  constraints.maxHeight > 0
              ? constraints.maxHeight
              : mediaSize.height;
          final availableSize = Size(
            math.max(0.0, fallbackLogicalWidth),
            math.max(0.0, fallbackLogicalHeight),
          );
          _gameDisplaySize = _containedGameDisplaySize(
            availableSize.width,
            availableSize.height,
          );
          _clampOverlayPosition(deferNotify: true);
          final consoleWidth = math.min(
            math.max(300.0, constraints.maxWidth * 0.46),
            math.max(0.0, constraints.maxWidth - 24),
          );

          return Stack(
            children: [
              Positioned.fill(
                child: ColoredBox(
                  color: Colors.transparent,
                  child: _gameDisplaySize.isEmpty
                      ? const SizedBox.shrink()
                      : Center(
                          child: SizedBox(
                            width: _gameDisplaySize.width,
                            height: _gameDisplaySize.height,
                            child: _GameSurfaceLayer(
                              onPointerDown: _handlePointerDown,
                              onPointerMove: _handlePointerMove,
                              onPointerUp: _handlePointerUp,
                              onPointerCancel: _handlePointerCancel,
                            ),
                          ),
                        ),
                ),
              ),
              if (_loadingConsole.active && consoleWidth > 0)
                Positioned(
                  left: 12,
                  top: 12,
                  bottom: 12,
                  width: consoleWidth,
                  child: IgnorePointer(
                    child: RepaintBoundary(
                      child: _StartupConsoleOverlay(snapshot: _loadingConsole),
                    ),
                  ),
                ),
              if (!_loadingConsole.active &&
                  _renderStats.showFps &&
                  _renderStats.available)
                Positioned(
                  left: 10,
                  top: 10,
                  child: IgnorePointer(
                    child: RepaintBoundary(
                      child: _FpsCounterOverlay(stats: _renderStats),
                    ),
                  ),
                ),
              _FloatingOverlayPositioner(
                offsetListenable: _overlayOffset,
                child: RepaintBoundary(
                  child: _menuMode
                      ? _FloatingMenuPanel(
                          future: _menuFuture,
                          onBack: () => setState(
                              () => _menuFuture = widget.bridge.getMainMenu()),
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
              ),
            ],
          );
        },
      ),
    );
  }
}

class _LoadingConsoleLine {
  const _LoadingConsoleLine({required this.message, required this.important});

  final String message;
  final bool important;
}

class _LoadingConsoleSnapshot {
  const _LoadingConsoleSnapshot({
    this.active = false,
    this.session = 0,
    this.totalLines = 0,
    this.lines = const <_LoadingConsoleLine>[],
  });

  final bool active;
  final int session;
  final int totalLines;
  final List<_LoadingConsoleLine> lines;

  factory _LoadingConsoleSnapshot.fromMap(Map<Object?, Object?> map) {
    final rawLines = map['lines'];
    final lines = <_LoadingConsoleLine>[];
    if (rawLines is Iterable) {
      for (final rawLine in rawLines) {
        if (rawLine is Map) {
          final message = rawLine['message']?.toString() ?? '';
          if (message.isEmpty) {
            continue;
          }
          lines.add(_LoadingConsoleLine(
              message: message, important: rawLine['important'] == true));
        }
      }
    }
    return _LoadingConsoleSnapshot(
      active: map['active'] == true,
      session: _readInt(map['session']),
      totalLines: _readInt(map['totalLines']),
      lines: lines,
    );
  }
}

class _RenderOverlayStats {
  const _RenderOverlayStats({
    this.showFps = false,
    this.available = false,
    this.fps = 0,
    this.drawCount = 0,
    this.videoMemoryBytes = 0,
    this.selfMemoryMb = 0,
    this.freeMemoryMb = 0,
    this.presentedFrames = 0,
    this.sequence = 0,
    this.rendererName = '',
  });

  final bool showFps;
  final bool available;
  final double fps;
  final int drawCount;
  final int videoMemoryBytes;
  final int selfMemoryMb;
  final int freeMemoryMb;
  final int presentedFrames;
  final int sequence;
  final String rendererName;

  factory _RenderOverlayStats.fromMap(Map<Object?, Object?> map) {
    return _RenderOverlayStats(
      showFps: map['showFps'] == true,
      available: map['available'] == true,
      fps: _readDouble(map['fps']),
      drawCount: _readInt(map['drawCount']),
      videoMemoryBytes: _readInt(map['videoMemoryBytes']),
      selfMemoryMb: _readInt(map['selfMemoryMb']),
      freeMemoryMb: _readInt(map['freeMemoryMb']),
      presentedFrames: _readInt(map['presentedFrames']),
      sequence: _readInt(map['sequence']),
      rendererName: map['rendererName']?.toString() ?? '',
    );
  }
}

int _readInt(Object? value) {
  if (value is int) {
    return value;
  }
  if (value is num) {
    return value.toInt();
  }
  return int.tryParse(value?.toString() ?? '') ?? 0;
}

double _readDouble(Object? value) {
  if (value is double) {
    return value;
  }
  if (value is num) {
    return value.toDouble();
  }
  return double.tryParse(value?.toString() ?? '') ?? 0;
}

class _StartupConsoleOverlay extends StatelessWidget {
  const _StartupConsoleOverlay({required this.snapshot});

  final _LoadingConsoleSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final maxVisibleLines =
            math.max(1, ((constraints.maxHeight - 16) / 16).floor()).toInt();
        final start = math.max(0, snapshot.lines.length - maxVisibleLines);
        final lines = snapshot.lines.sublist(start);
        return DecoratedBox(
          decoration: BoxDecoration(
            color: const Color(0x78000000),
            borderRadius: BorderRadius.circular(8),
          ),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.end,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                for (final line in lines)
                  Text(
                    line.message.length > 200
                        ? line.message.substring(0, 200)
                        : line.message,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      color: line.important
                          ? const Color(0xffffd75a)
                          : Colors.white.withValues(alpha: 0.76),
                      fontSize: 12.5,
                      height: 1.18,
                      shadows: const [
                        Shadow(color: Colors.black, blurRadius: 2)
                      ],
                    ),
                  ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _FpsCounterOverlay extends StatelessWidget {
  const _FpsCounterOverlay({required this.stats});

  final _RenderOverlayStats stats;

  @override
  Widget build(BuildContext context) {
    final videoMemoryMb = stats.videoMemoryBytes / (1024 * 1024);
    final renderer =
        stats.rendererName.isEmpty ? 'renderer' : stats.rendererName;
    final text = '${stats.fps.toStringAsFixed(1)} fps '
        '(${stats.drawCount} TVP ops, ${stats.presentedFrames} presents, $renderer)\n'
        '${stats.selfMemoryMb} MB(${videoMemoryMb.toStringAsFixed(2)} MB) ${stats.freeMemoryMb} MB';
    return Text(
      text,
      style: const TextStyle(
        color: Colors.white,
        fontSize: 12,
        height: 1.16,
        shadows: [
          Shadow(color: Colors.black, blurRadius: 2),
          Shadow(color: Colors.black, blurRadius: 4),
        ],
      ),
    );
  }
}

class _GameSurfaceLayer extends StatelessWidget {
  const _GameSurfaceLayer({
    required this.onPointerDown,
    required this.onPointerMove,
    required this.onPointerUp,
    required this.onPointerCancel,
  });

  final PointerDownEventListener onPointerDown;
  final PointerMoveEventListener onPointerMove;
  final PointerUpEventListener onPointerUp;
  final PointerCancelEventListener onPointerCancel;

  @override
  Widget build(BuildContext context) {
    return Listener(
      behavior: HitTestBehavior.opaque,
      onPointerDown: onPointerDown,
      onPointerMove: onPointerMove,
      onPointerUp: onPointerUp,
      onPointerCancel: onPointerCancel,
      child: const SizedBox.expand(),
    );
  }
}

class _FloatingOverlayPositioner extends StatelessWidget {
  const _FloatingOverlayPositioner({
    required this.offsetListenable,
    required this.child,
  });

  final ValueListenable<Offset> offsetListenable;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Positioned.fill(
      child: ValueListenableBuilder<Offset>(
        valueListenable: offsetListenable,
        child: child,
        builder: (context, offset, child) {
          return Align(
            alignment: Alignment.bottomRight,
            child: Transform.translate(
              offset: Offset(-offset.dx, -offset.dy),
              child: child!,
            ),
          );
        },
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
    final scheme = Theme.of(context).colorScheme;
    return Listener(
      behavior: HitTestBehavior.opaque,
      onPointerMove: expanded
          ? null
          : (event) => onDrag(
                DragUpdateDetails(
                  sourceTimeStamp: event.timeStamp,
                  delta: event.delta,
                  globalPosition: event.position,
                  localPosition: event.localPosition,
                ),
              ),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 140),
        curve: Curves.easeOutCubic,
        height: 56,
        decoration: BoxDecoration(
          color: scheme.surfaceContainerHighest.withValues(alpha: 0.92),
          borderRadius: BorderRadius.circular(28),
          border: Border.all(
            color: scheme.outlineVariant.withValues(alpha: 0.72),
          ),
          boxShadow: [
            BoxShadow(
                color: Colors.black.withValues(alpha: 0.24),
                blurRadius: 14,
                offset: const Offset(0, 6)),
          ],
        ),
        child: ClipRRect(
          borderRadius: BorderRadius.circular(28),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (expanded) ...[
                const SizedBox(width: 4),
                _TrayButton(
                    icon: Icons.menu_rounded,
                    label: '菜单',
                    onTap: () => onAction('game-menu')),
                _TrayButton(
                    icon: Icons.fit_screen_rounded,
                    label: '窗口',
                    onTap: () => onAction('window-manager')),
                _TrayButton(
                    icon: mouseMode
                        ? Icons.mouse_rounded
                        : Icons.touch_app_rounded,
                    label: mouseMode ? '鼠标' : '触摸',
                    onTap: () => onAction('mouse-mode')),
                _TrayButton(
                    icon: Icons.power_settings_new_rounded,
                    label: '退出',
                    destructive: true,
                    onTap: () => onAction('exit')),
                const SizedBox(width: 2),
                Container(width: 1, height: 28, color: scheme.outlineVariant),
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
    return IconButton(
      onPressed: onTap,
      tooltip: '展开',
      icon: const Icon(Icons.drag_indicator_rounded),
    );
  }
}

class _TrayButton extends StatelessWidget {
  const _TrayButton(
      {required this.icon,
      required this.label,
      required this.onTap,
      this.destructive = false});

  final IconData icon;
  final String label;
  final VoidCallback onTap;
  final bool destructive;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final foreground = destructive ? scheme.error : scheme.onSurfaceVariant;
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(18),
      child: SizedBox(
        width: 52,
        height: 56,
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(icon, size: 22, color: foreground),
            const SizedBox(height: 2),
            Text(
              label,
              maxLines: 1,
              overflow: TextOverflow.fade,
              softWrap: false,
              style: Theme.of(context)
                  .textTheme
                  .labelSmall
                  ?.copyWith(color: foreground, fontSize: 10),
            ),
          ],
        ),
      ),
    );
  }
}

class _FloatingMenuPanel extends StatelessWidget {
  const _FloatingMenuPanel(
      {required this.future,
      required this.onBack,
      required this.onClose,
      required this.onActivate});

  final Future<List<GameMenuItem>> future;
  final VoidCallback onBack;
  final VoidCallback onClose;
  final ValueChanged<GameMenuItem> onActivate;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final screen = MediaQuery.sizeOf(context);
    final width = math.min(360.0, math.max(260.0, screen.width - 24.0));
    final height = math.min(420.0, math.max(240.0, screen.height - 24.0));
    return SizedBox(
      width: width,
      height: height,
      child: Padding(
        padding: const EdgeInsets.all(6),
        child: ClipRRect(
          borderRadius: BorderRadius.circular(8),
          child: Material(
            color: scheme.surfaceContainerHigh.withValues(alpha: 0.94),
            clipBehavior: Clip.antiAlias,
            child: DecoratedBox(
              decoration: BoxDecoration(
                border: Border.all(
                  color: scheme.outlineVariant.withValues(alpha: 0.72),
                ),
                boxShadow: [
                  BoxShadow(
                      color: Colors.black.withValues(alpha: 0.24),
                      blurRadius: 18,
                      offset: const Offset(0, 8)),
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
                          icon: const Icon(Icons.arrow_back_rounded),
                          tooltip: '返回',
                        ),
                        const Icon(Icons.menu_rounded, size: 24),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            '游戏菜单',
                            style: Theme.of(context)
                                .textTheme
                                .titleSmall
                                ?.copyWith(fontWeight: FontWeight.w700),
                          ),
                        ),
                        IconButton(
                          visualDensity: VisualDensity.compact,
                          onPressed: onClose,
                          icon: const Icon(Icons.close_rounded),
                          tooltip: '收起',
                        ),
                      ],
                    ),
                  ),
                  Divider(height: 1, color: scheme.outlineVariant),
                  Expanded(
                    child: FutureBuilder<List<GameMenuItem>>(
                      future: future,
                      builder: (context, snapshot) {
                        final items = snapshot.data ?? const <GameMenuItem>[];
                        if (snapshot.connectionState ==
                            ConnectionState.waiting) {
                          return const Center(
                              child: SizedBox(
                                  width: 22,
                                  height: 22,
                                  child: CircularProgressIndicator(
                                      strokeWidth: 2)));
                        }
                        if (items.isEmpty) {
                          return const LauncherEmptyState(
                              icon: Icons.menu_open_rounded,
                              message: '当前游戏没有菜单');
                        }
                        return Scrollbar(
                          child: ListView.separated(
                            padding: const EdgeInsets.symmetric(vertical: 4),
                            itemCount: items.length,
                            separatorBuilder: (_, __) => Divider(
                                height: 1,
                                indent: 56,
                                color: scheme.outlineVariant),
                            itemBuilder: (context, index) => _FloatingMenuTile(
                                item: items[index], onTap: onActivate),
                          ),
                        );
                      },
                    ),
                  ),
                ],
              ),
            ),
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
    final hasChildren = item.children.isNotEmpty;
    final scheme = Theme.of(context).colorScheme;
    final enabledColor = item.enabled
        ? scheme.onSurface
        : scheme.onSurface.withValues(alpha: 0.38);
    return LauncherListItem(
      dense: true,
      leading: Icon(
        hasChildren
            ? Icons.folder_open_rounded
            : item.checked
                ? Icons.check_rounded
                : Icons.radio_button_unchecked_rounded,
        color: enabledColor,
        size: 20,
      ),
      title: Text(
        item.title.isEmpty ? '(未命名)' : item.title,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
        style: Theme.of(context)
            .textTheme
            .bodyMedium
            ?.copyWith(color: enabledColor),
      ),
      trailing: hasChildren
          ? Icon(Icons.chevron_right_rounded,
              color: scheme.onSurfaceVariant, size: 20)
          : null,
      onTap: item.enabled ? () => onTap(item) : null,
    );
  }
}
