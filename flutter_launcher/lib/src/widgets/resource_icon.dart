import 'package:flutter/material.dart';

class ResourceIcon extends StatelessWidget {
  const ResourceIcon(this.assetName, {super.key, this.size = 24});

  final String assetName;
  final double size;

  @override
  Widget build(BuildContext context) {
    return Image.asset(
      'assets/cocos-studio/img/$assetName',
      width: size,
      height: size,
      filterQuality: FilterQuality.medium,
      errorBuilder: (_, __, ___) => Icon(Icons.extension, size: size),
    );
  }
}
