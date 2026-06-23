#!/usr/bin/env bash
# Launch the xdic_webgui web app. Self-contained; does not touch the C++ build.
#
#   ./run.sh                 # serve on http://localhost:8021
#   PORT=9000 ./run.sh       # custom port
#   XDIC_REPO_ROOT=/path ./run.sh   # point param/data resolution at another repo checkout
#
# Requires: pip install -r requirements.txt (ideally inside a venv).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

# Default the repo root to two levels up (apps/xdic_webgui -> repo root) unless overridden.
export XDIC_REPO_ROOT="${XDIC_REPO_ROOT:-$(cd "$HERE/../.." && pwd)}"
PORT="${PORT:-8021}"

echo "xdic_webgui: repo root = $XDIC_REPO_ROOT"
echo "xdic_webgui: serving on http://localhost:$PORT"
exec uvicorn backend.main:app --host 0.0.0.0 --port "$PORT" "$@"
