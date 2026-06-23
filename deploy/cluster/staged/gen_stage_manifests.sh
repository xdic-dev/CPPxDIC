#!/usr/bin/env bash
# =============================================================================
# gen_stage_manifests.sh — emit the per-stage SLURM array manifests for the
# decomposed pipeline (matching -> tracking -> E+F).
#
# Produces three files in the current directory:
#   manifest_matching.txt   one line "trial pair"        per (trial, pair)
#   manifest_tracking.txt   one line "trial pair cam"     per (trial, pair, camera)
#   manifest_ef.txt         one line "trial"              per (trial)
#
# The (pair -> cameras) mapping MUST match Utils::getCamerasForPair() in the
# binary. For the standard S09 2-pair layout that is:
#     pair 1 -> cameras 1 2      (myDIC2DpairResults_C_1_C_2)
#     pair 2 -> cameras 4 3      (myDIC2DpairResults_C_4_C_3)
# Override CAMS_FOR_PAIR_<p> if your rig differs.
#
# USAGE:
#   TRIALS="7 12 25" PAIRS="1 2" ./gen_stage_manifests.sh
# =============================================================================
set -euo pipefail

TRIALS="${TRIALS:-7 12 25}"          # space-separated target trials
PAIRS="${PAIRS:-1 2}"                # space-separated stereopairs
CAMS_FOR_PAIR_1="${CAMS_FOR_PAIR_1:-1 2}"
CAMS_FOR_PAIR_2="${CAMS_FOR_PAIR_2:-4 3}"

cams_for_pair() {
    local p="$1"; local var="CAMS_FOR_PAIR_${p}"
    echo "${!var:-}"
}

: > manifest_matching.txt
: > manifest_tracking.txt
: > manifest_ef.txt

for t in $TRIALS; do
    echo "$t" >> manifest_ef.txt
    for p in $PAIRS; do
        echo "$t $p" >> manifest_matching.txt
        cams="$(cams_for_pair "$p")"
        if [[ -z "$cams" ]]; then
            echo "ERROR: no camera mapping for pair $p (set CAMS_FOR_PAIR_${p})" >&2
            exit 1
        fi
        for c in $cams; do
            echo "$t $p $c" >> manifest_tracking.txt
        done
    done
done

echo "Wrote:"
printf '  %-22s %d tasks\n' manifest_matching.txt "$(wc -l < manifest_matching.txt)"
printf '  %-22s %d tasks\n' manifest_tracking.txt "$(wc -l < manifest_tracking.txt)"
printf '  %-22s %d tasks\n' manifest_ef.txt       "$(wc -l < manifest_ef.txt)"
