#!/bin/bash
# =============================================================================
# Shared configuration for the CPU-vs-GPU (ncorr vs cuNCorr) comparison study
# on MANNEBACK. Sourced by build_sif.sbatch, run_one.sbatch, submit_all.sh,
# compare_bits.sh. Edit paths in ONE place.
# =============================================================================

# --- Repo / image ---------------------------------------------------------
# CISM home checkout of feat/cuncorr-integration (visible from Manneback).
export REPO="/home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC"
export DEF="${REPO}/deploy/cluster/cppxdic_gpu.def"
export CONFIGS="${REPO}/deploy/cluster/configs"

# --- Study root on Manneback globalscratch --------------------------------
export BASE="/globalscratch/ucl/inma/jaoga/cpugpu"
export SIF="${BASE}/cppxdic_gpu.sif"

# --- Input data (Manneback globalscratch) ----------------------------------
# Videos + protocol: $DATA/S09/speckles/coating/{vid,protocol}
# The pipeline resolves videos at <data_path>/rawdata/<subject>/speckles/...
# so DATA_ROOT holds a `rawdata` symlink pointing at $DATA (set up by
# submit_all.sh / build_sif.sbatch).
export DATA="/globalscratch/ucl/inma/jaoga/data"
export DATA_ROOT="${BASE}/data_root"          # data_path (contains rawdata -> $DATA)
export ANALYSIS_SRC="${DATA}/analysis"        # REF_*.mat + calib per subject

# --- Outputs ---------------------------------------------------------------
# Big DIC outputs on globalscratch (purgeable); small artifacts (run.log,
# meta.txt) UNDER THE REPO ON HOME — globalscratch was purged once already
# (2026-07) and only the copies on home survived.
export RUNS_DIR="${BASE}/runs"                        # isolated dic_path per run
export RESULTS_DIR="${REPO}/deploy/cluster/cpugpu/results"   # meta + logs (home)
export LOGS_DIR="${REPO}/deploy/cluster/cpugpu/logs"         # sbatch stdout/err (home)

# --- Apptainer -------------------------------------------------------------
export APPTAINER_CACHEDIR="${BASE}/.apptainer_cache"

# --- Experiment-invariant subject parameters -------------------------------
export SUBJECT="${SUBJECT:-S09}"
export REFTRIAL="${REFTRIAL:-5}"
export MATERIAL_DIR="coating"    # material_id=2 -> coating

# --- Bind: one root covers data + outputs ----------------------------------
export BIND_ROOT="/globalscratch/ucl/inma/jaoga"

mkdir -p "${RUNS_DIR}" "${RESULTS_DIR}" "${LOGS_DIR}" "${APPTAINER_CACHEDIR}" 2>/dev/null || true
# data_path staging: <data_path>/rawdata must resolve to $DATA
mkdir -p "${DATA_ROOT}" 2>/dev/null || true
[ -e "${DATA_ROOT}/rawdata" ] || ln -sfn "${DATA}" "${DATA_ROOT}/rawdata" 2>/dev/null || true
