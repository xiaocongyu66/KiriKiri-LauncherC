#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
asset_root="$repo_root/flutter_launcher/assets/cocos-studio"
source_root="$repo_root/ui/cocos-studio"

mkdir -p "$asset_root"
cp "$source_root/NotoSansCJK-Regular.ttc" "$asset_root/NotoSansCJK-Regular.ttc"
