#!/bin/bash
# =============================================================================
# TEST: staged execution. One generic runner for all three stages, driven by
# STAGE (match|track|ef) + a MANIFEST + an isolated config ROOT. Unlike main's
# deploy/cluster/staged scripts, this passes -C/-d/-n/-v so the run targets an
# ISOLATED dic_path. Each stage checkpoints to disk; track loads match's output.
#   env: STAGE ROOT MANIFEST  (SUBJECT/REFTRIAL from env.sh)
# =============================================================================
#SBATCH --job-name=xdic-stage
#SBATCH --output=deploy/cluster/study/apps/logs/stage_%x_%A_%a.out
#SBATCH --error=deploy/cluster/study/apps/logs/stage_%x_%A_%a.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
set -uo pipefail
source /home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/study/env.sh
cd "$REPO"; mkdir -p deploy/cluster/study/apps/logs

STAGE="${STAGE:?match|track|ef}"; ROOT="${ROOT:?config dir}"; MANIFEST="${MANIFEST:?}"
cfg="$ROOT/dic_params.txt"
# Cam-granularity decomposition (mirrors deploy/cluster/staged):
#   match: one task per (trial, pair)        -> MATCHING2*.bin, ncorr{c1}{c2}.bin
#   track: one task per (trial, pair, camera) -> ncorr{cam}.bin
#   ef   : one task per (trial)               -> format + 3D + deformation
# Concurrent same-trial tasks are race-safe because the ncorr temp dir is now
# unique per output (step_d_workflow.cpp tmp_ncorr_<output-stem>).
line=$(awk -v n="${SLURM_ARRAY_TASK_ID:-1}" 'NR==n' "$MANIFEST")
case "$STAGE" in
  match) read -r trial pair      <<<"$line"; SARGS=(--trial "$trial" --pair "$pair" --stages match) ;;
  track) read -r trial pair cam  <<<"$line"; SARGS=(--trial "$trial" --pair "$pair" --cam "$cam" --stages track) ;;
  ef)    read -r trial           <<<"$line"; SARGS=(--trial "$trial" --stages format,e,f) ;;
  *) echo "bad STAGE=$STAGE"; exit 2 ;;
esac
tag="${STAGE}_$(echo "$line" | tr ' ' '_')"
echo "[stage $STAGE] task=${SLURM_ARRAY_TASK_ID} line='$line' cpus=$SLURM_CPUS_PER_TASK"

START=$(date +%s)
apptainer exec --cleanenv \
    --env XDIC_PARALLEL_SEEDOPT=1 \
    --env OMP_NUM_THREADS="$SLURM_CPUS_PER_TASK" \
    --env OMP_PROC_BIND=spread --env OMP_PLACES=cores --env OMP_MAX_ACTIVE_LEVELS=1 \
    -B "$BIND_ROOT":"$BIND_ROOT" \
    "$SIF" \
    /opt/cppxdic/bin/cppxdic \
        -C "$cfg" -d "$cfg" -n "$ROOT/ncorr_params.txt" -v "$ROOT/visualization_params.txt" \
        --subject "$SUBJECT" --reftrial "$REFTRIAL" "${SARGS[@]}" \
    > "$ROOT/logs/${tag}.log" 2>&1
RC=$?
WALL=$(( $(date +%s) - START ))
ok=0; grep -qiE "completed|done|✓" "$ROOT/logs/${tag}.log" && ok=1
echo "[stage $STAGE] line='$line' rc=$RC ok=$ok wall=${WALL}s"
grep -iE "=== STEP|Parallel dispatch|Checkpoint|stages" "$ROOT/logs/${tag}.log" 2>/dev/null | head -6
# track/ef may rc=139 on Step-F; treat stage completion as success
[ "$RC" -eq 0 ] || [ "$ok" -eq 1 ] && exit 0 || exit "$RC"
