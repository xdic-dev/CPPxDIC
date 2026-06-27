#!/bin/bash
# =============================================================================
# Shared configuration for the CPPxDIC profiling study.
# Sourced by build_sif.sbatch, smoke_test.sbatch, run_one.sbatch, status.sh.
# Edit paths here in ONE place.
# =============================================================================

# --- Repo / image ---------------------------------------------------------
export REPO="/home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC"
export SIF="${REPO}/deploy/cluster/cppxdic_prof.sif"
export DEF="${REPO}/deploy/cluster/cppxdic_prof.def"
export CONFIGS="${REPO}/deploy/cluster/configs"   # ncorr / viz params live here

# --- Persistent data + outputs (globalscratch) ----------------------------
# DATA_PARENT is `data_path`: videos resolve at DATA_PARENT/rawdata/<subject>/...
export MULTIDIC="/globalscratch/ucl/inma/jaoga/MultiDIC"
export DATA_PARENT="${MULTIDIC}/example_data"
# ANALYSIS is the source tree holding REF_*.mat (under <subj>/<material>/) and
# calib (under <subj>/calib/). Each isolated job dic_path is seeded from here.
export ANALYSIS="${MULTIDIC}/analysis"

# Study outputs: full DIC results per experiment (runs/) + small profiling
# artifacts (results/). Both persist under the analysis tree.
export STUDY_OUT="${ANALYSIS}/_study"
export RUNS_DIR="${STUDY_OUT}/runs"        # full DIC outputs, one isolated dic_path per job
export RESULTS_DIR="${STUDY_OUT}/results"  # xprof CSVs + logs + meta per job

# --- Apptainer build scratch ----------------------------------------------
export APPTAINER_CACHEDIR="${MULTIDIC}/.apptainer_cache"

# --- Experiment-invariant subject parameters ------------------------------
export SUBJECT="${SUBJECT:-S09}"
export REFTRIAL="${REFTRIAL:-5}"
export MATERIAL_DIR="coating"   # material_id=2 -> coating (for REF seeding paths)

# --- Bind: a single globalscratch root covers data + analysis + outputs ----
export BIND_ROOT="${MULTIDIC}"

mkdir -p "${RUNS_DIR}" "${RESULTS_DIR}" "${APPTAINER_CACHEDIR}" 2>/dev/null || true
