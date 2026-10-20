#!/usr/bin/env bash
# Restores the Epic content (ThirdPerson template, level prototyping kit, input
# assets, Manny/Quinn mannequins) that is intentionally NOT stored in this repo.
# It is copied from your own Unreal Engine install, which is the licensed source.
#
# Usage:  Scripts/setup_assets.sh [--force]
#   UE_ROOT=/path/to/UE_5.8  Scripts/setup_assets.sh     # non-default engine location
set -euo pipefail

REQUIRED_VERSION="5.8"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONTENT="${PROJECT_DIR:-$REPO_ROOT/AdaptiveCompanion}/Content"
FORCE=0
[[ "${1:-}" == "--force" ]] && FORCE=1

case "$(uname -s)" in
  Darwin) DEFAULT_UE_ROOT="/Users/Shared/Epic Games/UE_${REQUIRED_VERSION}" ;;
  *)      DEFAULT_UE_ROOT="$HOME/UnrealEngine/UE_${REQUIRED_VERSION}" ;;
esac
UE_ROOT="${UE_ROOT:-$DEFAULT_UE_ROOT}"
TPL="$UE_ROOT/Templates"
RES="$TPL/TemplateResources/High"

if [[ ! -d "$TPL/TP_ThirdPerson/Content" ]]; then
  echo "error: Unreal Engine $REQUIRED_VERSION not found at: $UE_ROOT" >&2
  echo "Install it via the Epic Games Launcher or set UE_ROOT to its install folder." >&2
  exit 1
fi

# source -> destination (relative to Content/)
copy() {
  local src="$1" dst="$CONTENT/$2"
  [[ -d "$src" ]] || { echo "error: missing $src" >&2; exit 1; }
  mkdir -p "$dst"
  # macOS cp -n exits 1 when it skips existing files, so don't let that abort a re-run.
  if [[ $FORCE -eq 1 ]]; then cp -R "$src/." "$dst/"; else cp -Rn "$src/." "$dst/" || true; fi
  echo "  -> Content/$2"
}

echo "Restoring Epic content from $UE_ROOT"
copy "$TPL/TP_ThirdPerson/Content/ThirdPerson"        ThirdPerson
copy "$TPL/TP_ThirdPerson/Content/Variant_Combat"     Variant_Combat
copy "$TPL/TP_ThirdPerson/Content/__ExternalActors__" __ExternalActors__
copy "$TPL/TP_ThirdPerson/Content/__ExternalObjects__" __ExternalObjects__
copy "$RES/Input/Content"                             Input
copy "$RES/LevelPrototyping/Content"                  LevelPrototyping
copy "$RES/Characters/Content/Mannequins"            Characters/Mannequins

echo "Done. Open AdaptiveCompanion/AdaptiveCompanion.uproject."
