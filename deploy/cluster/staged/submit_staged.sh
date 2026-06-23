#!/usr/bin/env bash
# =============================================================================
# submit_staged.sh — submit the decomposed pipeline as three dependent SLURM
# array jobs: matching -> tracking -> (format + E + F).
#
#   matching  : one task per (trial, pair)
#   tracking  : one task per (trial, pair, camera)   [runs after matching ok]
#   ef        : one task per (trial)                  [runs after tracking ok]
#
# All tasks within a stage run in parallel (subject to the %CONC cap); the next
# stage starts only once the previous stage's array has fully succeeded.
#
# USAGE (binary):
#   cd <workdir with *_params.txt + ./build/cppxdic>
#   SUBJECT=S09 REFTRIAL=5 TRIALS="7 12 25" PAIRS="1 2" \
#     ./deploy/cluster/staged/submit_staged.sh
#
# USAGE (Singularity image):
#   SINGULARITY_IMAGE=/path/cppxdic.sif SUBJECT=S09 TRIALS="7 12 25" \
#     ./deploy/cluster/staged/submit_staged.sh
# =============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

SUBJECT="${SUBJECT:-S09}"
REFTRIAL="${REFTRIAL:-5}"
TRIALS="${TRIALS:-7 12 25}"
PAIRS="${PAIRS:-1 2}"
CONC="${CONC:-40}"                 # max concurrently-running array tasks per stage

# 1) Build the per-stage manifests in the current directory.
TRIALS="${TRIALS}" PAIRS="${PAIRS}" bash "${HERE}/gen_stage_manifests.sh"
N1=$(wc -l < manifest_matching.txt)
N2=$(wc -l < manifest_tracking.txt)
N3=$(wc -l < manifest_ef.txt)

EXPORT="ALL,SUBJECT=${SUBJECT},REFTRIAL=${REFTRIAL}"

# 2) Stage 1 — matching.
j1=$(sbatch --parsable --array=1-${N1}%${CONC} \
        --export="${EXPORT},MANIFEST=manifest_matching.txt" \
        "${HERE}/run_stage_matching.sbatch")
echo "stage 1 (matching): job ${j1}  (${N1} tasks)"

# 3) Stage 2 — tracking, after ALL matching tasks succeed.
j2=$(sbatch --parsable --dependency=afterok:${j1} --array=1-${N2}%${CONC} \
        --export="${EXPORT},MANIFEST=manifest_tracking.txt" \
        "${HERE}/run_stage_tracking.sbatch")
echo "stage 2 (tracking): job ${j2}  (${N2} tasks, after ${j1})"

# 4) Stage 3 — format + E + F, after ALL tracking tasks succeed.
j3=$(sbatch --parsable --dependency=afterok:${j2} --array=1-${N3}%${CONC} \
        --export="${EXPORT},MANIFEST=manifest_ef.txt" \
        "${HERE}/run_stage_ef.sbatch")
echo "stage 3 (format+E+F): job ${j3}  (${N3} tasks, after ${j2})"

echo
echo "Submitted. Monitor with:  squeue -u \$USER   or   ./deploy/cluster/monitor_job.sh --watch"
