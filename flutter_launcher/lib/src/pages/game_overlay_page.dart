import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/gestures.dart';
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
  bool _gameSurfaceInFlight = false;
  bool _gameSurfaceUnavailable = false;
  int? _gameTextureId;
  int _gameSurfaceWidth = 0;
  int _gameSurfaceHeight = 0;
  int _nativeFrameWidth = 0;
  int _nativeFrameHeight = 0;
  double _devicePixelRatio = 1.0;
  double _overlayRight = 18;
  double _overlayBottom = 18;
  Size _overlayBounds = Size.zero;
  Size _gameDisplaySize = Size.zero;
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
    final textureId = _gameTextureId;
    if (textureId != null) {
      unawaited(_invokeHost('disposeGameSurfaceTexture', {'textureId': textureId}));
    }
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
    unawaited(_refreshGameSurfaceMetrics());
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
    setState(() {
      _overlayRight = (_overlayRight - details.delta.dx).clamp(8.0, _maxOverlayRight).toDouble();
      _overlayBottom = (_overlayBottom - details.delta.dy).clamp(8.0, _maxOverlayBottom).toDouble();
    });
  }

  double get _maxOverlayRight => (_overlayBounds.width - 56).clamp(8.0, double.infinity).toDouble();

  double get _maxOverlayBottom => (_overlayBounds.height - 56).clamp(8.0, double.infinity).toDouble();

  Future<void> _invokeHost(String method, Map<String, Object?> arguments) async {
    try {
      await _channel.invokeMethod<void>(method, arguments);
    } on MissingPluginException {
      // iOS/desktop hosts can show the same Flutter overlay route without a
      // resize channel until their native overlay container is wired in.
    }
  }

  Future<Map<Object?, Object?>?> _invokeHostMap(String method, Map<String, Object?> arguments) async {
    try {
      return await _channel.invokeMapMethod<Object?, Object?>(method, arguments);
    } on MissingPluginException {
      return null;
    }
  }

  Future<void> _refreshGameSurfaceMetrics() async {
    final result = await _invokeHostMap('getGameSurfaceMetrics', const <String, Object?>{});
    if (!mounted || result == null) {
      return;
    }

    final width = (result['width'] as num?)?.toInt() ?? 0;
    final height = (result['height'] as num?)?.toInt() ?? 0;
    if (width <= 0 || height <= 0) {
      return;
    }

    if (width != _nativeFrameWidth || height != _nativeFrameHeight) {
      setState(() {
        _nativeFrameWidth = width;
        _nativeFrameHeight = height;
      });
    }

    final textureId = _gameTextureId;
    if (textureId != null && (_gameSurfaceWidth != width || _gameSurfaceHeight != height)) {
      unawaited(_ensureGameSurface(width, height));
    }
  }

  Future<void> _refreshLoadingConsole() async {
    final result = await _invokeHostMap('getLoadingConsoleSnapshot', const <String, Object?>{});
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
    final result = await _invokeHostMap('getRenderOverlayStats', const <String, Object?>{});
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

  void _scheduleGameSurface(int width, int height) {
    if (_gameSurfaceUnavailable || _gameSurfaceInFlight || width <= 0 || height <= 0) {
      return;
    }
    if (_gameTextureId != null && _gameSurfaceWidth == width && _gameSurfaceHeight == height) {
      return;
    }
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) {
        unawaited(_ensureGameSurface(width, height));
      }
    });
  }

  Future<void> _ensureGameSurface(int width, int height) async {
    if (_gameSurfaceUnavailable || _gameSurfaceInFlight || width <= 0 || height <= 0) {
      return;
    }
    if (_gameTextureId != null && _gameSurfaceWidth == width && _gameSurfaceHeight == height) {
      return;
    }
    _gameSurfaceInFlight = true;
    try {
      if (_gameTextureId == null) {
        final result = await _invokeHostMap('createGameSurfaceTexture', {'width': width, 'height': height});
        final textureId = result?['textureId'];
        if (textureId is! int) {
          _gameSurfaceUnavailable = true;
          return;
        }
        if (!mounted) {
          unawaited(_invokeHost('disposeGameSurfaceTexture', {'textureId': textureId}));
          return;
        }
        setState(() {
          _gameTextureId = textureId;
          _gameSurfaceWidth = width;
          _gameSurfaceHeight = height;
        });
        return;
      }

      final textureId = _gameTextureId!;
      final result = await _invokeHostMap('resizeGameSurfaceTexture', {
        'textureId': textureId,
        'width': width,
        'height': height,
      });
      if (result == null) {
        return;
      }
      if (mounted) {
        setState(() {
          _gameSurfaceWidth = width;
          _gameSurfaceHeight = height;
        });
      }
    } finally {
      _gameSurfaceInFlight = false;
    }
  }

  Offset _toPhysical(Offset viewPosition) {
    return Offset(viewPosition.dx * _devicePixelRatio, viewPosition.dy * _devicePixelRatio);
  }

  Future<void> _sendPointer(String method, PointerEvent event) async {
    final position = _toPhysical(event.position);
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
      'xs': entries.map((entry) => _toPhysical(entry.value).dx).toList(growable: false),
      'ys': entries.map((entry) => _toPhysical(entry.value).dy).toList(growable: false),
    });
  }

  void _handlePointerDown(PointerDownEvent event) {
    _activePointers[event.pointer] = event.position;
    unawaited(_sendPointer('gameTouchBegin', event));
  }

  void _handlePointerMove(PointerMoveEvent event) {
    if (!_activePointers.containsKey(event.pointer)) {
      return;
    }
    _activePointers[event.pointer] = event.position;
    unawaited(_sendPointerMove());
  }

  void _handlePointerUp(PointerUpEvent event) {
    _activePointers.remove(event.pointer);
    unawaited(_sendPointer('gameTouchEnd', event));
  }

  void _handlePointerCancel(PointerCancelEvent event) {
    final position = event.position;
    _activePointers[event.pointer] = position;
    final entries = _activePointers.entries.toList(growable: false);
    _activePointers.remove(event.pointer);
    unawaited(_invokeHost('gameTouchCancel', {
      'ids': entries.map((entry) => entry.key).toList(growable: false),
      'xs': entries.map((entry) => _toPhysical(entry.value).dx).toList(growable: false),
      'ys': entries.map((entry) => _toPhysical(entry.value).dy).toList(growable: false),
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
    _devicePixelRatio = MediaQuery.devicePixelRatioOf(context);
    return Material(
      type: MaterialType.transparency,
      child: LayoutBuilder(
        builder: (context, constraints) {
          _overlayBounds = Size(constraints.maxWidth, constraints.maxHeight);
          final frameWidth = _nativeFrameWidth > 0 ? _nativeFrameWidth : (_gameSurfaceWidth > 0 ? _gameSurfaceWidth : 1);
          final frameHeight = _nativeFrameHeight > 0 ? _nativeFrameHeight : (_gameSurfaceHeight > 0 ? _gameSurfaceHeight : 1);
          final requestedSurfaceWidth = _nativeFrameWidth > 0 ? _nativeFrameWidth : 1920;
          final requestedSurfaceHeight = _nativeFrameHeight > 0 ? _nativeFrameHeight : 1080;
          _scheduleGameSurface(requestedSurfaceWidth, requestedSurfaceHeight);
          _gameDisplaySize = _containSize(
            Size(frameWidth / _devicePixelRatio, frameHeight / _devicePixelRatio),
            Size(constraints.maxWidth, constraints.maxHeight),
          );
          _overlayRight = _overlayRight.clamp(8.0, _maxOverlayRight).toDouble();
          _overlayBottom = _overlayBottom.clamp(8.0, _maxOverlayBottom).toDouble();
          final consoleWidth = math.min(
            math.max(300.0, constraints.maxWidth * 0.46),
            math.max(0.0, constraints.maxWidth - 24),
          );

          return Stack(
            children: [
              Positioned.fill(
                child: ColoredBox(
                  color: Colors.black,
                  child: Center(
                    child: SizedBox(
                      width: _gameDisplaySize.width,
                      height: _gameDisplaySize.height,
                      child: _GameSurfaceLayer(
                        textureId: _gameTextureId,
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
              if (!_loadingConsole.active && _renderStats.showFps && _renderStats.available)
                Positioned(
                  left: 10,
                  top: 10,
                  child: IgnorePointer(
                    child: RepaintBoundary(
                      child: _FpsCounterOverlay(stats: _renderStats),
                    ),
                  ),
                ),
              Positioned(
                right: _overlayRight,
                bottom: _overlayBottom,
                child: RepaintBoundary(
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
              ),
            ],
          );
        },
      ),
    );
  }

  Size _containSize(Size content, Size bounds) {
    if (content.width <= 0 || content.height <= 0 || bounds.width <= 0 || bounds.height <= 0) {
      return Size.zero;
    }
    final scale = math.min(bounds.width / content.width, bounds.height / content.height);
    return Size(content.width * scale, content.height * scale);
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
          lines.add(_LoadingConsoleLine(message: message, important: rawLine['important'] == true));
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
        final maxVisibleLines = math.max(1, ((constraints.maxHeight - 16) / 16).floor()).toInt();
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
                    line.message.length > 200 ? line.message.substring(0, 200) : line.message,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      color: line.important ? const Color(0xffffd75a) : Colors.white.withOpacity(0.76),
                      fontSize: 12.5,
                      height: 1.18,
                      shadows: const [Shadow(color: Colors.black, blurRadius: 2)],
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
    final text = '${stats.fps.toStringAsFixed(1)} (${stats.drawCount} draws)\n'
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
    required this.textureId,
    required this.onPointerDown,
    required this.onPointerMove,
    required this.onPointerUp,
    required this.onPointerCancel,
  });

  final int? textureId;
  final PointerDownEventListener onPointerDown;
  final PointerMoveEventListener onPointerMove;
  final PointerUpEventListener onPointerUp;
  final PointerCancelEventListener onPointerCancel;

  @override
  Widget build(BuildContext context) {
    final textureId = this.textureId;
    return Listener(
      behavior: HitTestBehavior.opaque,
      onPointerDown: onPointerDown,
      onPointerMove: onPointerMove,
      onPointerUp: onPointerUp,
      onPointerCancel: onPointerCancel,
      child: textureId == null
          ? const SizedBox.expand()
          : Texture(textureId: textureId, filterQuality: FilterQuality.none),
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
