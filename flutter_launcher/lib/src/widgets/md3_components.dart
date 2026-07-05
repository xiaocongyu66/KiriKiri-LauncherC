import 'package:flutter/material.dart';

enum LauncherCardType { plain, filled }

class LauncherInfo {
  const LauncherInfo({required this.label, this.icon});

  final String label;
  final IconData? icon;
}

class LauncherInfoHeader extends StatelessWidget {
  const LauncherInfoHeader({
    required this.info,
    this.actions = const [],
    this.padding = const EdgeInsets.fromLTRB(16, 12, 12, 8),
    super.key,
  });

  final LauncherInfo info;
  final List<Widget> actions;
  final EdgeInsetsGeometry padding;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final scheme = theme.colorScheme;
    return Padding(
      padding: padding,
      child: Row(
        children: [
          if (info.icon != null) ...[
            Icon(info.icon, size: 20, color: scheme.onSurfaceVariant),
            const SizedBox(width: 8),
          ],
          Expanded(
            child: Text(
              info.label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: theme.textTheme.titleSmall?.copyWith(color: scheme.onSurfaceVariant),
            ),
          ),
          if (actions.isNotEmpty) ...[
            const SizedBox(width: 8),
            ...actions,
          ],
        ],
      ),
    );
  }
}

class LauncherCard extends StatelessWidget {
  const LauncherCard({
    required this.child,
    this.info,
    this.selected = false,
    this.type = LauncherCardType.plain,
    this.onPressed,
    this.onLongPress,
    this.padding = EdgeInsets.zero,
    this.isError = false,
    super.key,
  });

  final Widget child;
  final LauncherInfo? info;
  final bool selected;
  final LauncherCardType type;
  final VoidCallback? onPressed;
  final VoidCallback? onLongPress;
  final EdgeInsetsGeometry padding;
  final bool isError;

  BorderSide _borderSide(BuildContext context, Set<WidgetState> states) {
    final scheme = Theme.of(context).colorScheme;
    if (type == LauncherCardType.filled) {
      return BorderSide.none;
    }
    final baseColor = isError ? scheme.error : scheme.primary;
    if (selected) {
      return BorderSide(color: baseColor);
    }
    if (states.contains(WidgetState.hovered) || states.contains(WidgetState.focused) || states.contains(WidgetState.pressed)) {
      return BorderSide(color: baseColor.withOpacity(0.72));
    }
    return BorderSide(color: scheme.outlineVariant);
  }

  Color _backgroundColor(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    if (isError) {
      return selected ? scheme.errorContainer : scheme.surfaceContainerLow;
    }
    if (selected) {
      return scheme.secondaryContainer;
    }
    return type == LauncherCardType.filled ? scheme.surfaceContainerHigh : scheme.surfaceContainerLow;
  }

  Color _foregroundColor(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    if (isError) {
      return selected ? scheme.onErrorContainer : scheme.error;
    }
    return selected ? scheme.onSecondaryContainer : scheme.onSurfaceVariant;
  }

  @override
  Widget build(BuildContext context) {
    Widget content = child;
    if (info != null) {
      content = Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          LauncherInfoHeader(info: info!),
          Flexible(child: child),
        ],
      );
    }

    final shape = RoundedRectangleBorder(borderRadius: BorderRadius.circular(8));
    final background = _backgroundColor(context);
    final foreground = _foregroundColor(context);
    final style = ButtonStyle(
      elevation: const WidgetStatePropertyAll(0),
      padding: WidgetStatePropertyAll(padding),
      shape: WidgetStatePropertyAll(shape),
      backgroundColor: WidgetStatePropertyAll(background),
      foregroundColor: WidgetStatePropertyAll(foreground),
      overlayColor: WidgetStatePropertyAll(foreground.withOpacity(0.08)),
      side: WidgetStateProperty.resolveWith((states) => _borderSide(context, states)),
      tapTargetSize: MaterialTapTargetSize.shrinkWrap,
      visualDensity: VisualDensity.standard,
    );

    if (type == LauncherCardType.filled) {
      return FilledButton(
        clipBehavior: Clip.antiAlias,
        style: style,
        onPressed: onPressed,
        onLongPress: onLongPress,
        child: content,
      );
    }
    return OutlinedButton(
      clipBehavior: Clip.antiAlias,
      style: style,
      onPressed: onPressed,
      onLongPress: onLongPress,
      child: content,
    );
  }
}

class LauncherSection extends StatelessWidget {
  const LauncherSection({
    required this.info,
    required this.children,
    this.actions = const [],
    this.margin = const EdgeInsets.only(bottom: 12),
    super.key,
  });

  final LauncherInfo info;
  final List<Widget> children;
  final List<Widget> actions;
  final EdgeInsetsGeometry margin;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Padding(
      padding: margin,
      child: Material(
        color: scheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(8),
        clipBehavior: Clip.antiAlias,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            LauncherInfoHeader(info: info, actions: actions),
            Divider(height: 1, color: scheme.outlineVariant),
            ...children,
          ],
        ),
      ),
    );
  }
}

class LauncherListItem extends StatelessWidget {
  const LauncherListItem({
    required this.title,
    this.subtitle,
    this.leading,
    this.trailing,
    this.onTap,
    this.contentPadding = const EdgeInsets.symmetric(horizontal: 16),
    this.minVerticalPadding = 12,
    this.dense,
    super.key,
  })  : switchValue = null,
        onSwitchChanged = null;

  const LauncherListItem.switchItem({
    required this.title,
    required bool value,
    required ValueChanged<bool> onChanged,
    this.subtitle,
    this.leading,
    this.contentPadding = const EdgeInsets.only(left: 16, right: 8),
    this.minVerticalPadding = 8,
    this.dense,
    super.key,
  })  : trailing = null,
        onTap = null,
        switchValue = value,
        onSwitchChanged = onChanged;

  final Widget title;
  final Widget? subtitle;
  final Widget? leading;
  final Widget? trailing;
  final VoidCallback? onTap;
  final bool? switchValue;
  final ValueChanged<bool>? onSwitchChanged;
  final EdgeInsetsGeometry contentPadding;
  final double minVerticalPadding;
  final bool? dense;

  @override
  Widget build(BuildContext context) {
    final switchValue = this.switchValue;
    return ListTile(
      dense: dense,
      leading: leading,
      title: title,
      subtitle: subtitle,
      titleAlignment: ListTileTitleAlignment.center,
      minVerticalPadding: minVerticalPadding,
      contentPadding: contentPadding,
      onTap: switchValue == null ? onTap : () => onSwitchChanged?.call(!switchValue),
      trailing: switchValue == null ? trailing : Switch(value: switchValue, onChanged: onSwitchChanged),
    );
  }
}

class LauncherInfoRow extends StatelessWidget {
  const LauncherInfoRow(this.label, this.value, {super.key});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final scheme = theme.colorScheme;
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 10, 16, 10),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 88,
            child: Text(
              label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: theme.textTheme.bodySmall?.copyWith(color: scheme.onSurfaceVariant),
            ),
          ),
          const SizedBox(width: 12),
          Expanded(child: SelectableText(value.isEmpty ? '-' : value)),
        ],
      ),
    );
  }
}

class LauncherEmptyState extends StatelessWidget {
  const LauncherEmptyState({required this.icon, required this.message, this.action, super.key});

  final IconData icon;
  final String message;
  final Widget? action;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 40, color: scheme.onSurfaceVariant),
            const SizedBox(height: 8),
            Text(message, textAlign: TextAlign.center, style: Theme.of(context).textTheme.bodyMedium),
            if (action != null) ...[
              const SizedBox(height: 16),
              action!,
            ],
          ],
        ),
      ),
    );
  }
}
