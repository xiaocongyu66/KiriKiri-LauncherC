class GameEntry {
  const GameEntry({
    required this.title,
    required this.path,
    this.subtitle,
    this.coverPath,
    this.backgroundPath,
    this.launchFile,
    this.lastModified = 0,
    this.lastPlayedAt,
  });

  factory GameEntry.fromMap(Map<Object?, Object?> map) {
    return GameEntry(
      title: (map['title'] as String?)?.trim().isNotEmpty == true
          ? map['title'] as String
          : (map['path'] as String? ?? 'Untitled'),
      path: map['path'] as String? ?? map['gameDir'] as String? ?? '',
      subtitle: map['subtitle'] as String?,
      coverPath: map['coverPath'] as String?,
      backgroundPath: map['backgroundPath'] as String?,
      launchFile: map['launchFile'] as String?,
      lastModified: map['lastModified'] is int ? map['lastModified'] as int : 0,
      lastPlayedAt: map['lastPlayedAt'] is int
          ? DateTime.fromMillisecondsSinceEpoch(map['lastPlayedAt'] as int)
          : null,
    );
  }

  Map<String, Object?> toMap() => {
        'title': title,
        'path': path,
        'subtitle': subtitle,
        'coverPath': coverPath,
        'backgroundPath': backgroundPath,
        'launchFile': launchFile,
        'lastModified': lastModified,
        'lastPlayedAt': lastPlayedAt?.millisecondsSinceEpoch,
      };

  GameEntry copyWith({
    String? title,
    String? path,
    String? subtitle,
    String? coverPath,
    String? backgroundPath,
    String? launchFile,
    int? lastModified,
    DateTime? lastPlayedAt,
  }) {
    return GameEntry(
      title: title ?? this.title,
      path: path ?? this.path,
      subtitle: subtitle ?? this.subtitle,
      coverPath: coverPath ?? this.coverPath,
      backgroundPath: backgroundPath ?? this.backgroundPath,
      launchFile: launchFile ?? this.launchFile,
      lastModified: lastModified ?? this.lastModified,
      lastPlayedAt: lastPlayedAt ?? this.lastPlayedAt,
    );
  }

  final String title;
  final String path;
  final String? subtitle;
  final String? coverPath;
  final String? backgroundPath;
  final String? launchFile;
  final int lastModified;
  final DateTime? lastPlayedAt;
}
