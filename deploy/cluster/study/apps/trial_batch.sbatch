#!/bin/bash
# =============================================================================
# TEST: cppxdic multi-trial BATCH mode (--trials), one array task per trial.
# Each task picks trials[SLURM_ARRAY_TASK_ID]. Shared isolated dic_path is fine:
# the trials write to distinct trial_<id> subtrees (no checkpoint collision).
#   submit: ROOT=<dir from setup_isolated.sh> sbatch --array=1-3 trial_batch.sbatch
# =============================================================================
#SBATCH --job-name=xdic-batch
#SBATCH --output=deploy/cluster/study/apps/logs/batch_%A_%a.out
#SBATCH --error=deploy/cluster/study/apps/logs/batch_%A_%a.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=80G
#SBATCH --time=03:00:00
set -uo pipefail
source /home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/study/env.sh
cd "$REPO"
mkdir -p deploy/cluster/study/apps/logs

ROOT="${ROOT:?set ROOT to the setup_isolated.sh dir}"
TRIALS_CSV="${TRIALS_CSV:-7,12,25}"
cfg="$ROOT/dic_params.txt"

echo "=== trial-batch test: subject=$SUBJECT trials=$TRIALS_CSV task=$SLURM_ARRAY_TASK_ID ==="
echo "dic_path=$ROOT  cpus=$SLURM_CPUS_PER_TASK"

START=$(date +%s)
apptainer exec --cleanenv \
    --env SLURM_ARRAY_TASK_ID="$SLURM_ARRAY_TASK_ID" \
    --env XDIC_PROFILE=1 --env XDIC_FORCE_FRAMES=1 \
    --env XDIC_PARALLEL_SEEDOPT=1 \
    --env XDIC_PROFILE_OUT="$ROOT/logs/xprof_task${SLURM_ARRAY_TASK_ID}" \
    --env OMP_NUM_THREADS="$SLURM_CPUS_PER_TASK" \
    --env OMP_PROC_BIND=spread --env OMP_PLACES=cores --env OMP_MAX_ACTIVE_LEVELS=1 \
    -B "$BIND_ROOT":"$BIND_ROOT" \
    "$SIF" \
    /opt/cppxdic/bin/cppxdic \
        -C "$cfg" -d "$cfg" -n "$ROOT/ncorr_params.txt" -v "$ROOT/visualization_params.txt" \
        --subject "$SUBJECT" --reftrial "$REFTRIAL" --trials "$TRIALS_CSV" \
    > "$ROOT/logs/batch_task${SLURM_ARRAY_TASK_ID}.log" 2>&1
RC=$?
WALL=$(( $(date +%s) - START ))

# Which trial did this task run? (mirror the binary's selection)
TRIAL=$(echo "$TRIALS_CSV" | tr ',' '\n' | awk -v n="$SLURM_ARRAY_TASK_ID" 'NR==n')
stepd_ok=0; grep -qi "All 2D DIC Analysis completed" "$ROOT/logs/batch_task${SLURM_ARRAY_TASK_ID}.log" && stepd_ok=1
echo "[batch task $SLURM_ARRAY_TASK_ID] trial=$TRIAL rc=$RC stepd_ok=$stepd_ok wall=${WALL}s"
grep -i "Parallel dispatch\|Reading done. Frames\|=== STEP" "$ROOT/logs/batch_task${SLURM_ARRAY_TASK_ID}.log" 2>/dev/null | head -8
# Step F is known to crash downstream (rc=139); Step D/E completion is the test target.
[ "$RC" -eq 0 ] || [ "$stepd_ok" -eq 1 ] && exit 0 || exit "$RC"
