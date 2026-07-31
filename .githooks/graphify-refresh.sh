#!/bin/sh
# graphify-refresh.sh — incrementally rebuild the graphify knowledge graph
# after the working tree changes from a pull (post-merge / post-rewrite).
#
# Uses `graphify update` (AST-only, no LLM, no API key). Fast + deterministic.
# Sourced by .githooks/post-merge and .githooks/post-rewrite.

# Resolve repo root (hooks may run from an arbitrary cwd). Prefer git; fall back
# to this script's location (.githooks/ -> repo root is one dir up).
REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null)
if [ -z "$REPO_ROOT" ]; then
    HOOK_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
    REPO_ROOT=$(CDPATH='' cd -- "$HOOK_DIR/.." && pwd)
fi
cd "$REPO_ROOT" 2>/dev/null || exit 0

# Only run if a graph already exists — never bootstrap a full build from a hook.
[ -f graphify-out/graph.json ] || exit 0

# Locate graphify; bail quietly if unavailable so pulls never fail.
GRAPHIFY=$(command -v graphify 2>/dev/null)
[ -z "$GRAPHIFY" ] && [ -x "$HOME/.local/bin/graphify" ] && GRAPHIFY="$HOME/.local/bin/graphify"
[ -z "$GRAPHIFY" ] && exit 0

if ! "$GRAPHIFY" --help 2>&1 | grep -q -- '--force'; then
    echo "[graphify-refresh] update --force is unsupported (non-fatal); upgrade Graphify."
    exit 0
fi

echo "[graphify-refresh] Updating knowledge graph after pull..."
"$GRAPHIFY" update . --force >/dev/null 2>&1 \
  && echo "[graphify-refresh] graph.json and GRAPH_REPORT.md refreshed." \
  || echo "[graphify-refresh] update failed (non-fatal); run 'graphify update . --force' manually."

exit 0
