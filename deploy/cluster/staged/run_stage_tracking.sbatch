#!/bin/bash
# =============================================================================
# STAGE 2 — TRACKING.  One array task per (trial, pair, camera).
# Produces ncorr{cam}.bin. Loads the matching/ROI/seed checkpoints written by
# stage 1 (MATCHING2*.bin, ncorr{cam1}{cam2}.bin); it re-imports + filters the
# frame stack (those are not cached between stages — acceptable overhead).
#
# Resources: peak RSS is frame-stack + per-thread (~1.2 GB/thread). For more
# threads raise --cpus-per-task AND --mem together, and set step_d_total_threads
# in the config to match. Keep OMP_NUM_THREADS x step_d_total_threads <= cpus.
#
#   sbatch --array=1-$(wc -l < manifest_tracking.txt) \
#          --export=ALL,SUBJECT=S09,REFTRIAL=5 \
#          deploy/cluster/staged/run_stage_tracking.sbatch
# =============================================================================
#SBATCH --job-name=xdic-track
#SBATCH --output=logs/track_%A_%a.out
#SBATCH --error=logs/track_%A_%a.err
#SBATCH --time=03:00:00
#SBATCH --cpus-per-task=4
#SBATCH --mem=24G
set -euo pipefail

SUBJECT="${SUBJECT:-S09}"
REFTRIAL="${REFTRIAL:-5}"
MANIFEST="${MANIFEST:-manifest_tracking.txt}"
CPPXDIC_BIN="${CPPXDIC_BIN:-./build/cppxdic}"
SINGULARITY_IMAGE="${SINGULARITY_IMAGE:-}"
WORKDIR="${WORKDIR:-$PWD}"

mkdir -p logs
cd "${WORKDIR}"

# Pin OpenMP threads to the allocation (avoid oversubscription).
export OMP_NUM_THREADS="${OMP_NUM_THREADS:-${SLURM_CPUS_PER_TASK:-4}}"
export OMP_PROC_BIND=close OMP_PLACES=cores

line=$(awk -v n="${SLURM_ARRAY_TASK_ID}" 'NR==n' "${MANIFEST}")
read -r trial pair cam <<<"${line}"
[[ -n "${trial:-}" && -n "${pair:-}" && -n "${cam:-}" ]] || { echo "ERROR: bad manifest line ${SLURM_ARRAY_TASK_ID}: '${line}'" >&2; exit 1; }

echo "[track] task=${SLURM_ARRAY_TASK_ID} subject=${SUBJECT} trial=${trial} pair=${pair} cam=${cam} omp=${OMP_NUM_THREADS}"
ARGS=(--subject "${SUBJECT}" --reftrial "${REFTRIAL}" --trial "${trial}" --pair "${pair}" --cam "${cam}" --stages track)

if [[ -n "${SINGULARITY_IMAGE}" ]]; then
    singularity exec --env OMP_NUM_THREADS="${OMP_NUM_THREADS}" "${SINGULARITY_IMAGE}" cppxdic "${ARGS[@]}"
else
    "${CPPXDIC_BIN}" "${ARGS[@]}"
fi
