#!/bin/bash
# =============================================================================
# setup_isolated.sh — create an ISOLATED, checkpoint-safe work tree for an
# apps/mode test, seeded with S09 REF + calib symlinks and a ready dic_params.txt
# (parallel_processing=true, step_d_total_threads=THREADS). No compute; safe to
# run on the login node. Prints the created dir path on the last line.
#
#   usage:  setup_isolated.sh <name> <threads> [fstart fend fjump]
#   e.g.    setup_isolated.sh trial_batch 32 10 59 1
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/../../../.."        # repo root (CPPxDIC)
source deploy/cluster/study/env.sh

NAME="${1:?need a name}"
THREADS="${2:?need thread count}"
FSTART="${3:-10}"; FEND="${4:-59}"; FJUMP="${5:-1}"

ROOT="${RUNS_DIR}/apps/${NAME}"          # isolated dic_path + workdir
rm -rf "$ROOT"
mkdir -p "$ROOT/$SUBJECT/$MATERIAL_DIR" "$ROOT/logs"
# Seed REF + calib (same scheme as run_one.sbatch -> checkpoint isolation).
ln -sfn "$ANALYSIS/$SUBJECT/calib" "$ROOT/$SUBJECT/calib"
ln -sf  "$ANALYSIS/$SUBJECT/$MATERIAL_DIR/"REF_*.mat "$ROOT/$SUBJECT/$MATERIAL_DIR/" 2>/dev/null || true

# Config: base params + authoritative overrides (last value wins).
cp "$CONFIGS/dic_params.txt"           "$ROOT/dic_params.txt"
cp "$CONFIGS/ncorr_params.txt"         "$ROOT/ncorr_params.txt"
cp "$CONFIGS/visualization_params.txt" "$ROOT/visualization_params.txt"
cat >> "$ROOT/dic_params.txt" <<CFG

# ===== isolated apps-test overrides =====
base_path = $MULTIDIC
data_path = $DATA_PARENT
dic_path = $ROOT
idx_frame_start = $FSTART
idx_frame_end = $FEND
frame_jump = $FJUMP
parallel_processing = true
step_d_total_threads = $THREADS
step_e_total_threads = $THREADS
step_f_total_threads = $THREADS
CFG

echo "seeded REF:"; ls "$ROOT/$SUBJECT/$MATERIAL_DIR/" | grep REF | sed 's/^/  /' || echo "  (none)"
echo "$ROOT"
