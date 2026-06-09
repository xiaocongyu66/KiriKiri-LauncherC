class GameEntry {
  const GameEntry({
    required this.title,
    required this.path,
    this.subtitle,
    this.lastPlayedAt,
  });

  factory GameEntry.fromMap(Map<Object?, Object?> map) {
    return GameEntry(
      title: (map['title'] as String?)?.trim().isNotEmpty == true
          ? map['title'] as String
          : (map['path'] as String? ?? 'Untitled'),
      path: map['path'] as String? ?? '',
      subtitle: map['subtitle'] as String?,
      lastPlayedAt: map['lastPlayedAt'] is int
          ? DateTime.fromMillisecondsSinceEpoch(map['lastPlayedAt'] as int)
          : null,
    );
  }

  final String title;
  final String path;
  final String? subtitle;
  final DateTime? lastPlayedAt;
}
