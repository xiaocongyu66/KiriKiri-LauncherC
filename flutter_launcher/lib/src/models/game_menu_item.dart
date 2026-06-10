class GameMenuItem {
  const GameMenuItem({
    required this.title,
    required this.path,
    this.enabled = true,
    this.checked = false,
    this.separator = false,
    this.children = const [],
  });

  factory GameMenuItem.fromMap(Map<String, Object?> map) {
    return GameMenuItem(
      title: map['title'] as String? ?? '',
      path: map['path'] as String? ?? '',
      enabled: map['enabled'] as bool? ?? true,
      checked: map['checked'] as bool? ?? false,
      separator: map['separator'] as bool? ?? false,
      children: ((map['children'] as List?) ?? const [])
          .whereType<Map>()
          .map((child) => GameMenuItem.fromMap(child.cast<String, Object?>()))
          .toList(growable: false),
    );
  }

  final String title;
  final String path;
  final bool enabled;
  final bool checked;
  final bool separator;
  final List<GameMenuItem> children;
}
