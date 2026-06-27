#!/bin/bash
# =============================================================================
# Submit the full profiling grid as SLURM arrays.
# - One array per OMP value for the thread sweep (manifestA), so
#   --cpus-per-task / --mem / --time fit that slice tightly.
# - One array for the frame study (manifestB, all OMP=8).
# - Concurrency capped (%CAP) to stay a good citizen on a busy cluster.
# Each task runs run_one.sbatch in MANIFEST mode (isolated dic_path per row).
#
# Usage:  cd <repo root>; ./deploy/cluster/study/submit_grid.sh [CAP]
# =============================================================================
source /home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/study/env.sh
cd "$REPO"
SDIR=deploy/cluster/study
CAP="${1:-4}"                      # max concurrently-running tasks per array
RUNNER="$SDIR/run_one.sbatch"
[ -f "$SIF" ] || { echo "ERROR: image not built ($SIF). Run build_sif.sbatch first."; exit 1; }

bash "$SDIR/gen_manifest.sh"
mkdir -p "$SDIR/manifests"
: > "$SDIR/.grid_jobids"

submit() {  # $1=subManifest  $2=cpus  $3=mem  $4=time  $5=jobname
  local mf="$1" cpus="$2" mem="$3" tlimit="$4" name="$5"
  local n; n=$(wc -l < "$mf")
  [ "$n" -gt 0 ] || { echo "skip $mf (empty)"; return; }
  local id
  # %A_%a in the log paths: array tasks otherwise clobber one shared file
  # (this hid a task-1 startup failure in the 2026-06-23 run).
  id=$(sbatch --parsable \
        --job-name="$name" \
        --array=1-"$n"%"$CAP" \
        --cpus-per-task="$cpus" --mem="$mem" --time="$tlimit" \
        --output="$SDIR/logs/%x_%A_%a.out" \
        --error="$SDIR/logs/%x_%A_%a.err" \
        --export=ALL,MANIFEST="$PWD/$mf",PAR_SEEDOPT="${PAR_SEEDOPT:-0}" \
        "$RUNNER")
  echo "$id  $name  tasks=$n cpus=$cpus mem=$mem time=$tlimit cap=$CAP  ($mf)"
  echo "$id $name $mf" >> "$SDIR/.grid_jobids"
}

# ---- (A) thread sweep: split by THREAD count (manifest col 5), parallel ON ----
# 141-frame window (10-150 j1). cpus-per-task = thread count. The 4-thread arm is
# the slow corner; walltime is sized GENEROUSLY because a timeout SIGKILL cannot
# be caught (data loss). Memory grows with in-flight parallel frames (~1.7G/thr)
# plus speculative seed-opt, so high-thread arms request large mem to avoid OOM.
echo "PAR_SEEDOPT=${PAR_SEEDOPT:-0}"
declare -A A_MEM=(  [4]=48G [8]=64G [16]=96G [32]=160G [64]=224G )
declare -A A_TIME=( [4]=06:00:00 [8]=04:00:00 [16]=03:00:00 [32]=02:30:00 [64]=02:30:00 )
for n in 4 8 16 32 64; do
  sub="$SDIR/manifests/A_thr${n}.txt"
  awk -v n="$n" '$5==n' "$SDIR/manifestA.txt" > "$sub"
  submit "$sub" "$n" "${A_MEM[$n]}" "${A_TIME[$n]}" "xdic-study"
done

# ---- (B) frame study: omitted this run (manifestB empty). ----
if [ -s "$SDIR/manifestB.txt" ]; then
  cp "$SDIR/manifestB.txt" "$SDIR/manifests/B_thr8.txt"
  submit "$SDIR/manifests/B_thr8.txt" 8 64G 04:00:00 "xdic-study"
fi

echo
echo "submitted. watch with: $SDIR/status.sh --watch"
echo "cancel all grid jobs with: scancel \$(awk '{print \$1}' $SDIR/.grid_jobids)"
