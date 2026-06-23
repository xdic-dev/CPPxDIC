#!/bin/bash
# =============================================================================
# STAGE 1 — MATCHING.  One array task per (trial, pair).
# Produces ROI/seed + REF->trial matching (MATCHING2*.bin) and camera-to-camera
# matching (ncorr{cam1}{cam2}.bin) + dic_info_data_target_pair{pair}.mat.
#
# Memory note (from local profiling): importVideoFrames loads the WHOLE frame
# stack, so matching peaks at roughly the frame-stack size (~10 GB for 141
# frames). Time ~ import+saturation+ROI/seed+matching (~5 min).
#
# SIZE THE ARRAY to the number of lines in the manifest:
#   sbatch --array=1-$(wc -l < manifest_matching.txt) \
#          --export=ALL,SUBJECT=S09,REFTRIAL=5 \
#          deploy/cluster/staged/run_stage_matching.sbatch
# =============================================================================
#SBATCH --job-name=xdic-match
#SBATCH --output=logs/match_%A_%a.out
#SBATCH --error=logs/match_%A_%a.err
#SBATCH --time=00:45:00
#SBATCH --cpus-per-task=4
#SBATCH --mem=24G
set -euo pipefail

SUBJECT="${SUBJECT:-S09}"
REFTRIAL="${REFTRIAL:-5}"
MANIFEST="${MANIFEST:-manifest_matching.txt}"
CPPXDIC_BIN="${CPPXDIC_BIN:-./build/cppxdic}"
SINGULARITY_IMAGE="${SINGULARITY_IMAGE:-}"
WORKDIR="${WORKDIR:-$PWD}"

mkdir -p logs
cd "${WORKDIR}"

line=$(awk -v n="${SLURM_ARRAY_TASK_ID}" 'NR==n' "${MANIFEST}")
read -r trial pair <<<"${line}"
[[ -n "${trial:-}" && -n "${pair:-}" ]] || { echo "ERROR: bad manifest line ${SLURM_ARRAY_TASK_ID}: '${line}'" >&2; exit 1; }

echo "[match] task=${SLURM_ARRAY_TASK_ID} subject=${SUBJECT} reftrial=${REFTRIAL} trial=${trial} pair=${pair}"
ARGS=(--subject "${SUBJECT}" --reftrial "${REFTRIAL}" --trial "${trial}" --pair "${pair}" --stages match)

if [[ -n "${SINGULARITY_IMAGE}" ]]; then
    singularity exec "${SINGULARITY_IMAGE}" cppxdic "${ARGS[@]}"
else
    "${CPPXDIC_BIN}" "${ARGS[@]}"
fi
