#!/usr/bin/env bash
# Quits the Unreal editor, builds the editor target and opens the editor again.
#
# Building while the editor runs leaves Binaries/Mac/UnrealEditor.modules pointing at the old
# dylib, so new classes are missing after the next start. Save your work first: if the editor asks
# about unsaved changes, the script stops instead of forcing it to quit.
#
#   Scripts/rebuild_editor.sh            quit, build, reopen the editor if it was running
#   Scripts/rebuild_editor.sh --open     always open the editor after building
#   Scripts/rebuild_editor.sh --no-open  never open it
#
# Set UE_ROOT if the engine is not in the default location. macOS only.

set -euo pipefail

UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT="$REPO_ROOT/AdaptiveCompanion/AdaptiveCompanion.uproject"
EDITOR_APP="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app"
MCP_URL="http://localhost:8000/mcp"

editor_pid() {
	pgrep -f "MacOS/UnrealEditor .*AdaptiveCompanion.uproject" | head -1 || true
}

open_mode="auto"
case "${1:-}" in
	--open) open_mode="always" ;;
	--no-open) open_mode="never" ;;
	"") ;;
	*) echo "Unknown option: $1" >&2; exit 2 ;;
esac

was_running=false
pid="$(editor_pid)"
if [[ -n "$pid" ]]; then
	was_running=true
	echo "Quitting the editor (pid $pid)..."
	osascript -e 'quit app "UnrealEditor"' >/dev/null 2>&1 || true
	for _ in $(seq 1 60); do
		[[ -z "$(editor_pid)" ]] && break
		sleep 1
	done
	if [[ -n "$(editor_pid)" ]]; then
		echo "The editor is still running after 60 s, probably waiting in a save dialog. Save or discard, then run again." >&2
		exit 1
	fi
fi

echo "Building AdaptiveCompanionEditor..."
"$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" AdaptiveCompanionEditor Mac Development \
	-Project="$PROJECT" -WaitMutex

if [[ "$open_mode" == "always" || ( "$open_mode" == "auto" && "$was_running" == true ) ]]; then
	# the MCP server doesn't retry if the old editor's socket still holds the port
	for _ in $(seq 1 120); do
		netstat -an -p tcp | grep -q '127.0.0.1.8000 ' || break
		sleep 1
	done

	echo "Opening the editor..."
	open -a "$EDITOR_APP" --args "$PROJECT"
	for _ in $(seq 1 300); do
		if curl -s -o /dev/null -m 2 "$MCP_URL"; then
			echo "Editor is up (MCP answers at $MCP_URL)."
			exit 0
		fi
		sleep 2
	done
	echo "The editor didn't answer on $MCP_URL within 10 min." >&2
	exit 1
fi
