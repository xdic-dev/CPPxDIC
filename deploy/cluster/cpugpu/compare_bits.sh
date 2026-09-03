#!/bin/bash
# =============================================================================
# Bit-level comparison of two runs' Step-D outputs (MANNEBACK).
#
#   ./compare_bits.sh <runA> <runB>          # e.g. t7_ncorr_cpu32_r1 t7_cuncorr_gpu_r1
#   ./compare_bits.sh --all                  # canonical pairs for trials 7,12,25
#
# Method (per CPPxDIC/CLAUDE.md): NEVER md5 on .mat v7.3 — the HDF5 userblock
# embeds a creation timestamp and dic_path strings, so md5 always differs.
#   *.mat  -> h5diff (dataset-level, run inside the SIF)
#   *.bin  -> cmp (raw ncorr arrays, no metadata)
# Exit: 0 all compared files identical, 1 differences found, 2 missing files.
# =============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/env.sh"

H5DIFF() { apptainer exec -B "$BIND_ROOT":"$BIND_ROOT" "$SIF" h5diff "$@"; }

compare_pair() {
    local A="$RUNS_DIR/$1" B="$RUNS_DIR/$2"
    local ndiff=0 nmiss=0 nsame=0
    echo "=================================================================="
    echo "A = $1"
    echo "B = $2"
    [ -d "$A" ] || { echo "MISSING RUN DIR: $A"; return 2; }
    [ -d "$B" ] || { echo "MISSING RUN DIR: $B"; return 2; }

    # Compare every Step-D artifact present in A (skip seeded REF_* symlinks
    # and the copied config files).
    while IFS= read -r fa; do
        rel="${fa#"$A"/}"
        fb="$B/$rel"
        if [ ! -f "$fb" ]; then
            echo "  MISSING-IN-B: $rel"; nmiss=$((nmiss+1)); continue
        fi
        case "$fa" in
            *.mat)
                if out=$(H5DIFF "$fa" "$fb" 2>&1); then
                    echo "  IDENTICAL (h5diff): $rel"; nsame=$((nsame+1))
                else
                    n=$(echo "$out" | grep -oE "[0-9]+ differences? found" | head -1)
                    echo "  DIFFERS  (h5diff${n:+: $n}): $rel"; ndiff=$((ndiff+1))
                fi
                ;;
            *.bin)
                if cmp -s "$fa" "$fb"; then
                    echo "  IDENTICAL (cmp)   : $rel"; nsame=$((nsame+1))
                else
                    echo "  DIFFERS  (cmp)    : $rel"; ndiff=$((ndiff+1))
                fi
                ;;
        esac
    done < <(find "$A" \( -name "*.mat" -o -name "*.bin" \) -type f ! -name "REF_*" | sort)

    echo "  ---- summary: identical=$nsame differing=$ndiff missing=$nmiss"
    [ $nmiss -gt 0 ] && return 2
    [ $ndiff -gt 0 ] && return 1
    return 0
}

if [ "${1:-}" = "--all" ]; then
    overall=0
    for T in ${TRIALS:-7 12 25}; do
        echo; echo "############ TRIAL $T ############"
        echo "-- [GPU determinism] cuncorr gpu r1 vs r2"
        compare_pair "t${T}_cuncorr_gpu_r1" "t${T}_cuncorr_gpu_r2" || overall=1
        echo "-- [CPU vs GPU, same engine] cuncorr cpu vs cuncorr gpu"
        compare_pair "t${T}_cuncorr_cpu8_r1" "t${T}_cuncorr_gpu_r1" || overall=1
        echo "-- [engine] ncorr cpu32 vs cuncorr gpu"
        compare_pair "t${T}_ncorr_cpu32_r1" "t${T}_cuncorr_gpu_r1" || overall=1
    done
    exit $overall
else
    [ $# -eq 2 ] || { echo "usage: $0 <runA> <runB> | --all"; exit 2; }
    compare_pair "$1" "$2"
fi
