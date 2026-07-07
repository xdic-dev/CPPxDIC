#!/bin/bash
# =============================================================================
# Semantic comparison of the original MATLAB Step-D results (S09 trial 007,
# /globalscratch/.../cpugpu/matlab_ref/loading) against the cppxdic E1 (ncorr
# CPU 32thr) and E2 (cuncorr GPU) runs, using the matio-based compare_dic tool
# (h5diff is unusable here: MATLAB layouts differ / v5 files are not HDF5).
#
# Submit: sbatch deploy/cluster/cpugpu/compare_matlab.sbatch
# =============================================================================
#SBATCH --job-name=xdic-cmp-matlab
#SBATCH --output=/home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/logs/%x_%j.out
#SBATCH --error=/home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/logs/%x_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=64G
#SBATCH --time=03:00:00
#SBATCH --partition=keira
set -euo pipefail

source /home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/env.sh
HERE="$REPO/deploy/cluster/cpugpu"
ML=/globalscratch/ucl/inma/jaoga/cpugpu/matlab_ref/loading
E1=$RUNS_DIR/t7_ncorr_cpu32_r1/S09/coating/007/loading
E2=$RUNS_DIR/t7_cuncorr_gpu_r1/S09/coating/007/loading
OUT=$RESULTS_DIR/matlab_comparison
mkdir -p "$OUT"

# Build the comparator inside the SIF (matio + toolchain live there).
apptainer exec -B "$BIND_ROOT":"$BIND_ROOT" -B "$HERE":"$HERE" "$SIF" \
    g++ -O2 -o "$OUT/compare_dic" "$HERE/compare_dic.cpp" -lmatio
echo "comparator built"

# comparator exits 1 when differences exist — that's data, not an error, so the
# whole pipeline is wrapped (pipefail would otherwise kill the job; see CLAUDE.md).
run_cmp() { # label fileA fileB varA [varB]
    echo "########## $1  ($4${5:+ vs $5})"
    { apptainer exec -B "$BIND_ROOT":"$BIND_ROOT" "$SIF" \
        "$OUT/compare_dic" "$2" "$3" "$4" ${5:+"$5"} 2>&1 | \
        grep -vE '^IDENTICAL'; } || true   # keep logs readable; totals include all
    echo
}

for target in E1:$E1 E2:$E2; do
    lbl=${target%%:*}; dir=${target##*:}
    echo "=================================================================="
    echo "===== MATLAB vs $lbl"
    echo "=================================================================="
    # Final Step-D product. MATLAB wraps fields in a DIC2DpairResults struct
    # (v5 file); cppxdic stores them as top-level variables (v7.3) — compare
    # field by field via the dotted A-path / bare B-name form.
    for p in C_1_C_2 C_4_C_3; do
        for f in Points CorCoeffVec Faces FaceColors ROImask nCamRef nCamDef ncorrInfo; do
            run_cmp "myDIC2DpairResults_$p .$f MATLAB-vs-$lbl" \
                    "$ML/myDIC2DpairResults_$p.mat" "$dir/myDIC2DpairResults_$p.mat" \
                    "DIC2DpairResults.$f" "$f"
        done
    done
    # dic_info (v5 both-side small info files)
    for pr in pair1 pair2; do
        for f in idxframe actual_fps_meas; do
            run_cmp "dic_info_$pr .$f MATLAB-vs-$lbl" \
                    "$ML/dic_info_data_target_$pr.mat" "$dir/dic_info_data_target_$pr.mat" \
                    "$f"
        done
    done
    # Tracking + matching ncorr files: the displacement payload
    for f in ncorr1 ncorr2 ncorr3 ncorr4 ncorr12 ncorr43 MATCHING2005_pair1 MATCHING2005_pair2; do
        run_cmp "$f MATLAB-vs-$lbl" "$ML/$f.mat" "$dir/$f.mat" data_dic_save
    done
done
echo "ALL COMPARISONS DONE"
