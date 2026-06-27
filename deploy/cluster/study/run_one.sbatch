#!/bin/bash
# =============================================================================
# Run ONE profiling experiment in an isolated, checkpoint-safe output tree.
#
# Each experiment gets its OWN dic_path ($RUNS_DIR/<name>) seeded with symlinks
# to the shared REF_*.mat + calib. Because the pipeline's checkpoint system keys
# only on dic_path/subject/material/trial/phase (NOT on threads/frames), two
# experiments that shared an output dir would false-skip each other — isolation
# per dic_path is what makes parallel jobs independent.
#
# Parameterised via env vars (all have defaults for the validation run):
#   TRIAL FSTART FEND FJUMP OMP STEPD REP   (+ SUBJECT REFTRIAL from env.sh)
#
# Examples:
#   sbatch --cpus-per-task=8 --mem=24G --time=01:00:00 \
#          deploy/cluster/study/run_one.sbatch                       # defaults
#   TRIAL=12 FSTART=10 FEND=150 OMP=1 STEPD=16 \
#     sbatch --cpus-per-task=16 --mem=40G --time=03:00:00 \
#            deploy/cluster/study/run_one.sbatch
# =============================================================================
#SBATCH --job-name=xdic-run
#SBATCH --output=deploy/cluster/study/logs/%x_%j.out
#SBATCH --error=deploy/cluster/study/logs/%x_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=24G
#SBATCH --time=01:00:00
set -euo pipefail

source /home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/study/env.sh
cd "$REPO"

# ---- experiment parameters ----
# Array mode: if MANIFEST is set, read this task's row (1-based, no header):
#   columns = trial fstart fend fjump threads par rep
#   threads = step_d_total_threads = the REAL Step-D thread axis (frame-level
#             OpenMP in CppNCorr's matlab path). OMP_NUM_THREADS is set to match
#             but does NOT drive it (it gets overridden by num_threads()).
#   par     = parallel_processing (1=parallel matlab path, 0=sequential).
#             See CPPxDIC/CLAUDE.md for why par MUST be 1 to get any threading.
if [ -n "${MANIFEST:-}" ]; then
    line=$(awk -v n="${SLURM_ARRAY_TASK_ID:-1}" 'NR==n' "$MANIFEST")
    read TRIAL FSTART FEND FJUMP THREADS PAR REP <<<"$line"
    echo "manifest row ${SLURM_ARRAY_TASK_ID}: $line"
fi
# Defaults (used when not in manifest mode).
TRIAL="${TRIAL:-7}"
FSTART="${FSTART:-10}"
FEND="${FEND:-39}"
FJUMP="${FJUMP:-1}"
THREADS="${THREADS:-8}"   # step_d_total_threads AND OMP_NUM_THREADS AND cpus
PAR="${PAR:-1}"           # parallel_processing flag
REP="${REP:-1}"
OMP="$THREADS"            # keep OMP_NUM_THREADS aligned to the real thread count

# Live phase optimization (default off -> baseline behaviour).
# PAR_SEEDOPT -> XDIC_PARALLEL_SEEDOPT (parallel per-frame seed optimization; big win).
# The old parallel-import (#1) and ref-frame-1 (#1b) knobs were superseded by
# upstream PR #35 (seek-once sequential read + maxFrames) and removed.
PAR_SEEDOPT="${PAR_SEEDOPT:-0}"
# Encode the opt flag in NAME so A/B runs get their own isolated dic_path.
OPTSUF=""
if [ "$PAR_SEEDOPT" = 1 ]; then OPTSUF="_ps${PAR_SEEDOPT}"; fi

NAME="t${TRIAL}_f${FSTART}-${FEND}j${FJUMP}_thr${THREADS}_par${PAR}_r${REP}${OPTSUF}"
ROOT="${RUNS_DIR}/${NAME}"        # isolated dic_path (full DIC outputs land here)
RES="${RESULTS_DIR}/${NAME}"      # small profiling artifacts (CSVs/logs/meta)

echo "=============================================="
echo "[$NAME] job=$SLURM_JOB_ID node=$(hostname)"
echo "cpus=$SLURM_CPUS_PER_TASK mem=${SLURM_MEM_PER_NODE:-?}M  threads=$THREADS par=$PAR OMP_NUM_THREADS=$OMP"
echo "guard: threads=$THREADS must be <= cpus-per-task=$SLURM_CPUS_PER_TASK"
echo "dic_path=$ROOT"
echo "=============================================="
if [ "$THREADS" -gt "${SLURM_CPUS_PER_TASK:-1}" ]; then
    echo "ERROR: threads ($THREADS) exceeds --cpus-per-task ($SLURM_CPUS_PER_TASK) -> oversubscription"
    exit 2
fi

# Sanity: SIF must exist
[ -f "$SIF" ] || { echo "ERROR: image not found: $SIF (build it first)"; exit 1; }

# ---- seed isolated dic_path with REF + calib symlinks ----
# Wipe any prior outputs so this is always a FRESH measurement: the checkpoint
# system would otherwise load existing step results and skip computation,
# reporting ~0 tracking time. Each task owns a unique $ROOT, so this is safe.
rm -rf "$ROOT" "$RES"
mkdir -p "$ROOT/$SUBJECT/$MATERIAL_DIR" "$RES"
ln -sfn "$ANALYSIS/$SUBJECT/calib" "$ROOT/$SUBJECT/calib"
ln -sf  "$ANALYSIS/$SUBJECT/$MATERIAL_DIR/"REF_*.mat "$ROOT/$SUBJECT/$MATERIAL_DIR/"
echo "Seeded REF files:"; ls -l "$ROOT/$SUBJECT/$MATERIAL_DIR/" | grep REF || echo "  (none found - check ANALYSIS path)"

# ---- per-job config: copy base dic_params + append authoritative overrides ----
cfg="$ROOT/dic_params.txt"
cp "$CONFIGS/dic_params.txt" "$cfg"
cp "$CONFIGS/ncorr_params.txt" "$ROOT/ncorr_params.txt"
cp "$CONFIGS/visualization_params.txt" "$ROOT/visualization_params.txt"
cat >> "$cfg" <<CFG

# ===== study overrides (appended; last value wins) =====
base_path = $MULTIDIC
data_path = $DATA_PARENT
dic_path = $ROOT
idx_frame_start = $FSTART
idx_frame_end = $FEND
frame_jump = $FJUMP
# parallel_processing gates the matlab parallel path; step_d_total_threads is
# the frame-level OpenMP thread count (requested_threads). BOTH are required —
# par=0 or threads=1 => sequential. See CPPxDIC/CLAUDE.md.
parallel_processing = $([ "$PAR" -eq 1 ] && echo true || echo false)
step_d_total_threads = $THREADS
CFG

# ---- run (timed) inside the container ----
TIMER=""
[ -x /usr/bin/time ] && TIMER="/usr/bin/time -v"

# Optional optimization switches passed into the container only when enabled.
OPT_ENV=()
if [ "$PAR_SEEDOPT" = 1 ]; then OPT_ENV+=(--env XDIC_PARALLEL_SEEDOPT=1); fi
echo "optimizations: PAR_SEEDOPT=$PAR_SEEDOPT"

START=$(date +%s)
set +e
$TIMER apptainer exec --cleanenv \
    --env XDIC_PROFILE=1 \
    --env XDIC_FORCE_FRAMES=1 \
    --env XDIC_PROFILE_OUT="$RES" \
    --env OMP_NUM_THREADS="$OMP" \
    --env OMP_PROC_BIND=spread \
    --env OMP_PLACES=cores \
    --env OMP_MAX_ACTIVE_LEVELS=1 \
    --env OMP_DYNAMIC=false \
    "${OPT_ENV[@]}" \
    -B "$BIND_ROOT":"$BIND_ROOT" \
    "$SIF" \
    /opt/cppxdic/bin/cppxdic \
        -C "$cfg" -d "$cfg" -n "$ROOT/ncorr_params.txt" -v "$ROOT/visualization_params.txt" \
        --subject "$SUBJECT" --reftrial "$REFTRIAL" --trial "$TRIAL" \
    > "$RES/run.log" 2>&1
RC=$?
set -e
END=$(date +%s)
WALL=$((END-START))

# ---- classify outcome (Step D is the profiling target) ----
stepd_ok=0; grep -qi "All 2D DIC Analysis completed" "$RES/run.log" && stepd_ok=1
xprof_ok=0; [ -f "$RES/xprof_phases.csv" ] && xprof_ok=1
# Parallelism verification: the matlab parallel path prints "Parallel dispatch"
# and a per-thread summary. If par=1 we REQUIRE this, else the run silently went
# sequential (the 2026-06-23 bug) and the data point is worthless.
# NOTE: `set -euo pipefail` is active — every extraction below MUST be pipefail-safe.
# A no-match grep returns 1, and `head -1` SIGPIPEs upstream `sort` (also non-zero
# under pipefail). Both would abort the script before meta.txt is written, so each
# pipeline ends with `|| true` and the regexes match the real dispatch-log format
# (`Requesting       = N threads`).
dispatch_n=$(grep -cE "matlab_run_segment_dic\] Parallel dispatch" "$RES/run.log" 2>/dev/null || true)
dispatch_n=${dispatch_n:-0}
threads_obs=$(grep -oE "Requesting[ =]+[0-9]+ threads" "$RES/run.log" 2>/dev/null | grep -oE "[0-9]+" | sort -rn | head -1 || true)
speedup=$(grep -oE "speedup = [0-9.]+" "$RES/run.log" 2>/dev/null | grep -oE "[0-9.]+" | sort -rn | head -1 || true)
parallel_ok=0; { [ "${dispatch_n:-0}" -gt 0 ] && parallel_ok=1; } || true

status="failed"; exit_code="$RC"
if [ "$RC" -eq 0 ]; then
    status="ok"; exit_code=0
elif [ "$RC" -eq 139 ] && [ "$stepd_ok" -eq 1 ] && [ "$xprof_ok" -eq 1 ]; then
    # Known: Step E stitching can segfault; Step D + profiling are complete.
    status="stepD_ok_downstream_crash(rc=139)"; exit_code=0
fi
# Loud warning if we asked for parallel but it ran sequential anyway.
if [ "$PAR" -eq 1 ] && [ "$stepd_ok" -eq 1 ] && [ "$parallel_ok" -eq 0 ]; then
    status="${status}__WARN_RAN_SEQUENTIAL"
    echo "WARNING: par=1 requested but NO 'Parallel dispatch' in run.log -> ran sequential!"
fi

# ---- record metadata + contention sentinel ----
{
  echo "name=$NAME"
  echo "job_id=$SLURM_JOB_ID"
  echo "node=$(hostname)"
  echo "rc=$RC"
  echo "status=$status"
  echo "stepd_ok=$stepd_ok"
  echo "parallel_ok=$parallel_ok"
  echo "threads_requested=$THREADS"
  echo "threads_observed=${threads_obs:-NA}"
  echo "speedup=${speedup:-NA}"
  echo "wall_s=$WALL"
  echo "params: trial=$TRIAL fstart=$FSTART fend=$FEND fjump=$FJUMP threads=$THREADS par=$PAR rep=$REP par_seedopt=$PAR_SEEDOPT"
  echo "cpus_per_task=${SLURM_CPUS_PER_TASK:-?}"
  # per-invocation matching_cams (xprof total is summed over both pairs: total_ms=$3, count=$2)
  awk -F, '/D.matching_cams/{printf "matching_cams_s=%.1f\n",$3/($2>0?$2:1)/1000}' "$RES/xprof_phases.csv" 2>/dev/null
  awk '/Maximum resident set size/{print "maxrss_kb="$NF}' "$RES/run.log" 2>/dev/null
} > "$RES/meta.txt"

echo "---- meta ----"; cat "$RES/meta.txt"
echo "---- xprof phases ----"; cat "$RES/xprof_phases.csv" 2>/dev/null || echo "(no xprof_phases.csv)"
echo "[$NAME] rc=$RC status=$status wall=${WALL}s  (full outputs: $ROOT ; artifacts: $RES)"
exit $exit_code
