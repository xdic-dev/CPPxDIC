#!/bin/bash
# =============================================================================
# Reproduce the Step-E stitching segfault under gdb and capture a backtrace.
# Reuses an existing run's dic_path (Step-D outputs checkpointed there), runs
# ONLY --stages e with the RelWithDebInfo binary (cppxdic_dbg) inside the SIF.
#
#   RUN=t7_cuncorr_gpu_Sall_r1 sbatch deploy/cluster/cpugpu/debug_stepe.sbatch
# =============================================================================
#SBATCH --job-name=xdic-dbg-stepe
#SBATCH --output=/home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/logs/%x_%j.out
#SBATCH --error=/home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/logs/%x_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=64G
#SBATCH --time=02:00:00
#SBATCH --partition=keira
set -euo pipefail

source /home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/env.sh
RUN="${RUN:-t7_cuncorr_gpu_Sall_r1}"
ROOT="$RUNS_DIR/$RUN"
[ -d "$ROOT" ] || { echo "ERROR: run dir $ROOT missing"; exit 1; }

echo "== gdb Step-E reproduction on $RUN ($(hostname)) =="
set +e
apptainer exec --cleanenv \
    --env OMP_NUM_THREADS=8 \
    -B "$BIND_ROOT":"$BIND_ROOT" \
    "$SIF" \
    gdb -batch \
        -ex "set pagination off" \
        -ex "run" \
        -ex "echo \n===== BACKTRACE =====\n" \
        -ex "bt" \
        -ex "echo \n===== BACKTRACE FULL (top 8) =====\n" \
        -ex "bt full 8" \
        --args /opt/cppxdic/bin/cppxdic_dbg \
        -C "$ROOT/dic_params.txt" -d "$ROOT/dic_params.txt" \
        -n "$ROOT/ncorr_params.txt" -v "$ROOT/visualization_params.txt" \
        --subject "$SUBJECT" --reftrial "$REFTRIAL" --trial "${TRIAL:-7}" \
        --log-level "${LOGLEVEL:-info}" \
        --stages "${STAGES:-e}"
RC=$?
set -e
echo "== gdb session ended rc=$RC =="
