#!/bin/bash
# =============================================================================
# STAGE 3 — FORMAT + STEP E + STEP F.  One array task per (trial).
# Runs formatOutput for every pair (combines ncorr{cam1},{cam2},{cam1cam2} ->
# myDIC2DpairResults), then 3D reconstruction (DIC3Dcombined) and deformation
# (DIC3DPPresults). Inputs are all on disk from stage 2; no frames are loaded,
# so this stage is light and fast (E ~ seconds, F ~ tens of seconds).
#
#   sbatch --array=1-$(wc -l < manifest_ef.txt) \
#          --export=ALL,SUBJECT=S09,REFTRIAL=5 \
#          deploy/cluster/staged/run_stage_ef.sbatch
# =============================================================================
#SBATCH --job-name=xdic-ef
#SBATCH --output=logs/ef_%A_%a.out
#SBATCH --error=logs/ef_%A_%a.err
#SBATCH --time=00:30:00
#SBATCH --cpus-per-task=2
#SBATCH --mem=16G
set -euo pipefail

SUBJECT="${SUBJECT:-S09}"
REFTRIAL="${REFTRIAL:-5}"
MANIFEST="${MANIFEST:-manifest_ef.txt}"
CPPXDIC_BIN="${CPPXDIC_BIN:-./build/cppxdic}"
SINGULARITY_IMAGE="${SINGULARITY_IMAGE:-}"
WORKDIR="${WORKDIR:-$PWD}"

mkdir -p logs
cd "${WORKDIR}"

line=$(awk -v n="${SLURM_ARRAY_TASK_ID}" 'NR==n' "${MANIFEST}")
read -r trial <<<"${line}"
[[ -n "${trial:-}" ]] || { echo "ERROR: bad manifest line ${SLURM_ARRAY_TASK_ID}: '${line}'" >&2; exit 1; }

echo "[ef] task=${SLURM_ARRAY_TASK_ID} subject=${SUBJECT} trial=${trial}"
ARGS=(--subject "${SUBJECT}" --reftrial "${REFTRIAL}" --trial "${trial}" --stages format,e,f)

if [[ -n "${SINGULARITY_IMAGE}" ]]; then
    singularity exec "${SINGULARITY_IMAGE}" cppxdic "${ARGS[@]}"
else
    "${CPPXDIC_BIN}" "${ARGS[@]}"
fi
