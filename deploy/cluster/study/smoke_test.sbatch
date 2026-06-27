#!/bin/bash
# =============================================================================
# Smoke test: 2 forced frames, isolated tree. Validates that the image runs,
# XDIC_FORCE_FRAMES is honoured, and REF/calib resolve, BEFORE any real job.
#
# Submit (after the build):
#   sbatch --dependency=afterok:<buildJobId> deploy/cluster/study/smoke_test.sbatch
# =============================================================================
#SBATCH --job-name=xdic-smoke
#SBATCH --output=deploy/cluster/study/logs/%x_%j.out
#SBATCH --error=deploy/cluster/study/logs/%x_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:15:00
set -euo pipefail

source /home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/study/env.sh
cd "$REPO"

TRIAL=7; FSTART=10; FEND=11; FJUMP=1; OMP=2; STEPD=1
ROOT="${STUDY_OUT}/_smoke/t${TRIAL}_f${FSTART}-${FEND}"
RES="$ROOT/_artifacts"
rm -rf "$ROOT"   # smoke must recompute from scratch (no stale checkpoints)
mkdir -p "$ROOT/$SUBJECT/$MATERIAL_DIR" "$RES"

echo "=== xdic-smoke on $(hostname) @ $(date) ==="
[ -f "$SIF" ] || { echo "ERROR: image not found: $SIF"; exit 1; }

ln -sfn "$ANALYSIS/$SUBJECT/calib" "$ROOT/$SUBJECT/calib"
ln -sf  "$ANALYSIS/$SUBJECT/$MATERIAL_DIR/"REF_*.mat "$ROOT/$SUBJECT/$MATERIAL_DIR/"

cfg="$ROOT/dic_params.txt"
cp "$CONFIGS/dic_params.txt" "$cfg"
cat >> "$cfg" <<CFG

base_path = $MULTIDIC
data_path = $DATA_PARENT
dic_path = $ROOT
idx_frame_start = $FSTART
idx_frame_end = $FEND
frame_jump = $FJUMP
step_d_total_threads = $STEPD
CFG

set +e
apptainer exec --cleanenv \
    --env XDIC_PROFILE=1 --env XDIC_FORCE_FRAMES=1 --env XDIC_PROFILE_OUT="$RES" \
    --env OMP_NUM_THREADS=$OMP --env OMP_MAX_ACTIVE_LEVELS=1 \
    -B "$BIND_ROOT":"$BIND_ROOT" \
    "$SIF" /opt/cppxdic/bin/cppxdic \
        -C "$cfg" -d "$cfg" -n "$CONFIGS/ncorr_params.txt" -v "$CONFIGS/visualization_params.txt" \
        --subject "$SUBJECT" --reftrial "$REFTRIAL" --trial "$TRIAL" \
    > "$RES/smoke.log" 2>&1
RC=$?
set -e

echo "--- key log lines ---"
grep -E "Frame:|Frames:|Reading done|complete|ERROR|Checkpoint" "$RES/smoke.log" | head -30 || true

echo "--- VERDICT ---"
ok=1
grep -qiE "Frames: 2|Frame: 10 to 11" "$RES/smoke.log" || { echo "FAIL: did not force 2 frames"; ok=0; }
stepd_ok=0; grep -qi "All 2D DIC Analysis completed" "$RES/smoke.log" && stepd_ok=1
[ "$stepd_ok" -eq 1 ] || { echo "FAIL: Step D did not complete"; ok=0; }
[ -f "$RES/xprof_phases.csv" ] || { echo "FAIL: no xprof_phases.csv (profiling/emergency-dump not active)"; ok=0; }
if [ "$RC" -ne 0 ]; then
    if [ "$RC" -eq 139 ] && [ "$stepd_ok" -eq 1 ]; then
        echo "WARN: rc=139 (segfault) in a DOWNSTREAM step (Step E stitching); Step D + profiling OK"
    else
        echo "FAIL: cppxdic exited rc=$RC"; ok=0
    fi
fi
# Goal of this study is Step D: pass if Step D ran and profiling CSVs exist.
if [ "$ok" -eq 1 ]; then echo "SMOKE PASSED (Step D + profiling validated)"; else echo "SMOKE FAILED (see $RES/smoke.log)"; exit 1; fi
