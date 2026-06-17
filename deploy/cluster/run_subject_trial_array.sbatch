#!/bin/bash
# =============================================================================
# run_subject_trial_array.sbatch - SLURM array over (subject, trial) pairs from
# subject_trial.csv (run mode "c").
#
# Each array task runs ONE (subject, trial) pair. SLURM_ARRAY_TASK_ID (1-based)
# selects the Nth DATA line of subject_trial.csv (the header line is skipped),
# setting both the subject and the trial for that task. cppxdic does the row
# selection internally from SLURM_ARRAY_TASK_ID.
#
# SIZING THE ARRAY:
#   The array index range must equal the number of DATA rows (lines minus the
#   header). Compute it from the CSV:
#       N=$(($(wc -l < subject_trial.csv) - 1))   # subtract the header line
#   or, robust to blank/comment lines and a trailing newline-less file:
#       N=$(grep -vcE '^\s*(#|subject,|$)' subject_trial.csv)
#   then:
#       sbatch --array=1-${N} run_subject_trial_array.sbatch
#   gen_subject_trial also prints the exact "--array=1-N" line to use.
#
# USAGE:
#   N=$(grep -vcE '^\s*(#|subject,|$)' subject_trial.csv)
#   sbatch --array=1-${N} \
#          --export=ALL,SUBJECT_TRIAL_CSV=subject_trial.csv \
#          deploy/cluster/run_subject_trial_array.sbatch
# =============================================================================
#SBATCH --job-name=cppxdic-st
#SBATCH --output=logs/cppxdic_st_%A_%a.out
#SBATCH --error=logs/cppxdic_st_%A_%a.err
#SBATCH --array=1-1
#SBATCH --time=04:00:00
#SBATCH --cpus-per-task=6
#SBATCH --mem=16G

set -euo pipefail

# ---- User-configurable knobs (override via --export=ALL,VAR=value) ----------
SUBJECT_TRIAL_CSV="${SUBJECT_TRIAL_CSV:-subject_trial.csv}"
CPPXDIC_BIN="${CPPXDIC_BIN:-./build/cppxdic}"
SINGULARITY_IMAGE="${SINGULARITY_IMAGE:-}" # optional .sif; if set, run via singularity
WORKDIR="${WORKDIR:-$PWD}"                  # dir containing the CSV + *_params.txt

mkdir -p logs
cd "${WORKDIR}"

if [[ ! -f "${SUBJECT_TRIAL_CSV}" ]]; then
    echo "ERROR: CSV not found: ${SUBJECT_TRIAL_CSV}" >&2
    exit 1
fi

echo "Array task ${SLURM_ARRAY_TASK_ID} / job ${SLURM_ARRAY_JOB_ID}"
echo "CSV=${SUBJECT_TRIAL_CSV} -> selecting data row ${SLURM_ARRAY_TASK_ID}"

# cppxdic reads SLURM_ARRAY_TASK_ID itself, skips the header, and picks the Nth
# (subject, trial) data row -- setting both subject and trial for this task.
if [[ -n "${SINGULARITY_IMAGE}" ]]; then
    singularity exec --env SLURM_ARRAY_TASK_ID="${SLURM_ARRAY_TASK_ID}" \
        "${SINGULARITY_IMAGE}" cppxdic --subject-trial-csv "${SUBJECT_TRIAL_CSV}"
else
    "${CPPXDIC_BIN}" --subject-trial-csv "${SUBJECT_TRIAL_CSV}"
fi
