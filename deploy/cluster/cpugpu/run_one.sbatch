#!/bin/bash
# =============================================================================
# Run ONE CPU-vs-GPU comparison experiment in an isolated, checkpoint-safe
# output tree on MANNEBACK (Step D only: --stages d, so no Step-E/F downstream).
#
# Axes (env vars):
#   TRIAL   trial number                      (default 7)
#   ENGINE  ncorr | cuncorr                   (default cuncorr)
#   USE_GPU 1 -> apptainer --nv (CUDA)        (default 0)
#   THREADS step_d_total_threads for ncorr    (default 32; unused by cuncorr)
#   REP     repetition tag                    (default 1)
#
# Each run gets its OWN dic_path ($RUNS_DIR/<name>) seeded with REF/calib
# symlinks — the checkpoint system keys on dic_path/subject/material/trial
# only, so shared output dirs false-skip (see CPPxDIC/CLAUDE.md).
#
# Submit examples (see submit_all.sh):
#   TRIAL=7 ENGINE=ncorr   THREADS=32 sbatch -p keira -c 32 --mem=96G -t 06:00:00 run_one.sbatch
#   TRIAL=7 ENGINE=cuncorr USE_GPU=1  sbatch -p gpu --gres=gpu:1 -c 8 --mem=48G -t 08:00:00 run_one.sbatch
# =============================================================================
#SBATCH --job-name=xdic-cpugpu
#SBATCH --output=/home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/logs/%x_%j.out
#SBATCH --error=/home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/logs/%x_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
set -euo pipefail

source /home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/env.sh
cd "$REPO"

TRIAL="${TRIAL:-7}"
ENGINE="${ENGINE:-cuncorr}"
USE_GPU="${USE_GPU:-0}"
THREADS="${THREADS:-32}"
REP="${REP:-1}"
EXACT_MATLAB="${EXACT_MATLAB:-0}"  # ncorr only: exact_matlab_DIC_analysis_* path
UPDATES="${UPDATES:-0}"            # ncorr only: KEEP_MOST_POINTS + update_corrcoef=0.5
                                   # (correlation-based ref updates, MATLAB-style)
FIXEDSTEP="${FIXEDSTEP:-0}"        # ncorr only: >0 forces a reference change every N
                                   # frames (MATLAB step_ref_change semantics)
STAGES="${STAGES:-d}"              # --stages spec: d (Step D only, default) | all | ...

case "$ENGINE" in ncorr|cuncorr) ;; *) echo "ERROR: ENGINE must be ncorr|cuncorr"; exit 2;; esac

MODE="cpu${THREADS}"
[ "$USE_GPU" = 1 ] && MODE="gpu"
SUF=""
[ "$EXACT_MATLAB" = 1 ] && SUF="${SUF}_xm"
[ "$UPDATES" = 1 ] && SUF="${SUF}_upd"
[ "$FIXEDSTEP" != 0 ] && SUF="${SUF}_fs${FIXEDSTEP}"
[ "$STAGES" != d ] && SUF="${SUF}_S${STAGES//,/-}"
NAME="t${TRIAL}_${ENGINE}_${MODE}${SUF}_r${REP}"
ROOT="${RUNS_DIR}/${NAME}"     # isolated dic_path (full DIC outputs, globalscratch)
RES="${RESULTS_DIR}/${NAME}"   # run.log + meta.txt (repo on home — purge-safe)

echo "=============================================="
echo "[$NAME] job=${SLURM_JOB_ID:-?} node=$(hostname)"
echo "engine=$ENGINE gpu=$USE_GPU threads=$THREADS cpus=${SLURM_CPUS_PER_TASK:-?} mem=${SLURM_MEM_PER_NODE:-?}M"
echo "dic_path=$ROOT"
nvidia-smi -L 2>/dev/null || echo "(no visible GPU)"
echo "=============================================="

if [ "$ENGINE" = "ncorr" ] && [ "$THREADS" -gt "${SLURM_CPUS_PER_TASK:-1}" ]; then
    echo "ERROR: THREADS ($THREADS) exceeds --cpus-per-task (${SLURM_CPUS_PER_TASK:-1})"
    exit 2
fi
[ -f "$SIF" ] || { echo "ERROR: image not found: $SIF (build it first)"; exit 1; }

# ---- seed isolated dic_path with REF + calib symlinks ----
# Wipe prior outputs so every run is a FRESH computation (checkpoint system
# would otherwise skip work). Each run owns a unique $ROOT, so this is safe.
rm -rf "$ROOT" "$RES"
mkdir -p "$ROOT/$SUBJECT/$MATERIAL_DIR" "$RES"
ln -sfn "$ANALYSIS_SRC/$SUBJECT/calib" "$ROOT/$SUBJECT/calib"
ln -sf  "$ANALYSIS_SRC/$SUBJECT/$MATERIAL_DIR/"REF_*.mat "$ROOT/$SUBJECT/$MATERIAL_DIR/"
echo "Seeded REF files:"
ls -l "$ROOT/$SUBJECT/$MATERIAL_DIR/" | grep REF || { echo "ERROR: no REF files seeded from $ANALYSIS_SRC"; exit 1; }

# ---- per-run config: copy base params + append authoritative overrides ----
cfg="$ROOT/dic_params.txt"
cp "$CONFIGS/dic_params.txt" "$cfg"
cp "$CONFIGS/ncorr_params.txt" "$ROOT/ncorr_params.txt"
cp "$CONFIGS/visualization_params.txt" "$ROOT/visualization_params.txt"
cat >> "$cfg" <<CFG

# ===== cpugpu study overrides (appended; last value wins) =====
base_path = $DATA_ROOT
data_path = $DATA_ROOT
dic_path = $ROOT
idx_frame_start = 10
idx_frame_end = 150
frame_jump = 1
# ncorr engine: BOTH must be set for real threading (see CPPxDIC/CLAUDE.md).
parallel_processing = true
step_d_total_threads = $THREADS
# Engine selection — the compiled-in default is cuncorr, so ALWAYS explicit:
dic_engine = $ENGINE
CFG
# CAREFUL: ncorr_* params are parsed from the NCORR params file
# (Config::loadFromNcorrParamsFile), NOT from dic_params — appending them to
# $cfg is silently ignored (cost one E4 run to learn).
ncfg="$ROOT/ncorr_params.txt"
if [ "$EXACT_MATLAB" = 1 ]; then
    echo "use_exact_matlab = true" >> "$ncfg"
fi
if [ "$UPDATES" = 1 ]; then
    # MATLAB-style correlation-based reference updates: KEEP_MOST_POINTS preset
    # + update_corrcoef=0.5 (the workflow overrides update_corrcoef from
    # cutoff_corrcoef).
    echo "no_update = false" >> "$ncfg"
    echo "cutoff_corrcoef = 0.5" >> "$ncfg"
fi
if [ "$FIXEDSTEP" != 0 ]; then
    echo "fixed_step_ref = $FIXEDSTEP" >> "$ncfg"
fi

TIMER=""
[ -x /usr/bin/time ] && TIMER="/usr/bin/time -v"

GPU_FLAG=()
[ "$USE_GPU" = 1 ] && GPU_FLAG=(--nv)

START=$(date +%s)
set +e
$TIMER apptainer exec --cleanenv "${GPU_FLAG[@]}" \
    --env OMP_NUM_THREADS="$THREADS" \
    --env OMP_PROC_BIND=spread \
    --env OMP_PLACES=cores \
    --env OMP_MAX_ACTIVE_LEVELS=1 \
    --env OMP_DYNAMIC=false \
    -B "$BIND_ROOT":"$BIND_ROOT" \
    "$SIF" \
    /opt/cppxdic/bin/cppxdic \
        -C "$cfg" -d "$cfg" -n "$ROOT/ncorr_params.txt" -v "$ROOT/visualization_params.txt" \
        --subject "$SUBJECT" --reftrial "$REFTRIAL" --trial "$TRIAL" \
        --stages "$STAGES" \
    > "$RES/run.log" 2>&1
RC=$?
set -e
END=$(date +%s)
WALL=$((END-START))

# ---- classify (pipefail-safe: EVERY pipeline ends in `|| true`) ----
stepd_ok=0; grep -qi "All 2D DIC Analysis completed" "$RES/run.log" && stepd_ok=1 || true
# Engine verification — refuse to trust a run that silently used the wrong path.
backend=""
if [ "$ENGINE" = "cuncorr" ]; then
    backend=$(grep -oE "cuNCorr backend: .*\((CUDA|CPU)\)" "$RES/run.log" | head -1 || true)
    engine_ok=0; grep -q "Using cuNCorr DIC engine" "$RES/run.log" && engine_ok=1 || true
    cuda_used=0; echo "$backend" | grep -q "(CUDA)" && cuda_used=1 || true
    if [ "$USE_GPU" = 1 ] && [ "$cuda_used" -ne 1 ]; then
        echo "WARNING: GPU requested but cuNCorr did NOT report CUDA backend!"
    fi
else
    engine_ok=0; grep -qE "Parallel dispatch" "$RES/run.log" && engine_ok=1 || true
    cuda_used=0
fi
threads_obs=$(grep -oE "Requesting[ =]+[0-9]+ threads" "$RES/run.log" | grep -oE "[0-9]+" | sort -rn | head -1 || true)

status="failed"
if [ "$RC" -eq 0 ] && [ "$stepd_ok" -eq 1 ]; then status="ok"; fi

{
  echo "name=$NAME"
  echo "job_id=${SLURM_JOB_ID:-NA}"
  echo "node=$(hostname)"
  echo "rc=$RC"
  echo "status=$status"
  echo "stepd_ok=$stepd_ok"
  echo "engine=$ENGINE"
  echo "engine_ok=$engine_ok"
  echo "cuda_used=$cuda_used"
  echo "backend_line=${backend:-NA}"
  echo "threads_requested=$THREADS"
  echo "threads_observed=${threads_obs:-NA}"
  echo "wall_s=$WALL"
  echo "params: trial=$TRIAL engine=$ENGINE gpu=$USE_GPU threads=$THREADS rep=$REP stages=$STAGES exact_matlab=$EXACT_MATLAB updates=$UPDATES fixedstep=$FIXEDSTEP"
  echo "cpus_per_task=${SLURM_CPUS_PER_TASK:-?}"
  # full-pipeline progress markers (relevant when STAGES includes e/f)
  stepf_started=$(grep -c "Starting Deformation/Strain Analysis" "$RES/run.log" || true)
  # NB: pattern must not match Step D's "All 2D DIC Analysis completed successfully!"
  all_completed=$(grep -c "] Analysis completed successfully" "$RES/run.log" || true)
  echo "stepf_started=${stepf_started:-0}"
  echo "all_completed=${all_completed:-0}"
  awk '/Maximum resident set size/{print "maxrss_kb="$NF}' "$RES/run.log" || true
} > "$RES/meta.txt"
if [ "$RC" -ne 0 ]; then
    echo "---- run.log tail (rc=$RC) ----"
    tail -25 "$RES/run.log" || true
fi

echo "---- meta ----"; cat "$RES/meta.txt"
echo "---- output tree (Step-D artifacts) ----"
find "$ROOT/$SUBJECT" -name "*.mat" -o -name "*.bin" 2>/dev/null | grep -v REF_ | head -30 || true
echo "[$NAME] rc=$RC status=$status wall=${WALL}s  (outputs: $ROOT ; artifacts: $RES)"
[ "$status" = "ok" ] || exit 1
exit 0
