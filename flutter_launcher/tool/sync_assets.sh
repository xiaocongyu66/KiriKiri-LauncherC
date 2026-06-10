#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
asset_root="$repo_root/flutter_launcher/assets/cocos-studio"
source_root="$repo_root/ui/cocos-studio"

mkdir -p "$asset_root/img"
for asset in \
  menu_icon.png \
  menu_handler.png \
  touch_icon.png \
  mouse_icon.png \
  keyboard_icon.png \
  windows_icon.png \
  exit_icon.png \
  about_icon.png \
  syssetting_btn_on.png \
  syssetting_btn_ff.png; do
  cp "$source_root/img/$asset" "$asset_root/img/$asset"
done
cp "$source_root/NotoSansCJK-Regular.ttc" "$asset_root/NotoSansCJK-Regular.ttc"
