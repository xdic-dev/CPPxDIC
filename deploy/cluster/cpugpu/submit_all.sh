#!/bin/bash
# =============================================================================
# Submit the full CPU-vs-GPU grid on MANNEBACK: S09 trials {7,12,25} x
#   E1  ncorr   CPU 32 threads   (production CPU baseline; keira, 256-core Milan)
#   E2  cuncorr GPU (CUDA)       (gpu partition, A100; r1+r2 to also check
#                                 GPU run-to-run determinism)
#   E3  cuncorr CPU backend      (same binary as E2, no --nv -> CPU fallback;
#                                 attributes E1-vs-E2 differences: engine vs CUDA.
#                                 cuNCorr's CPU path is single-threaded -> long walltime)
#
# Usage:  ./submit_all.sh            # everything
#         ONLY=E1 ./submit_all.sh    # one experiment class (E1|E2|E3)
#         TRIALS="7" ./submit_all.sh
# =============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/env.sh"

TRIALS="${TRIALS:-7 12 25}"
ONLY="${ONLY:-all}"
RUN="$HERE/run_one.sbatch"

submit() { # label, then sbatch args...
    local label="$1"; shift
    local jid
    jid=$(sbatch --parsable "$@" "$RUN")
    echo "  $label -> job $jid"
}

for T in $TRIALS; do
    echo "=== trial $T ==="
    if [ "$ONLY" = all ] || [ "$ONLY" = E1 ]; then
        TRIAL=$T ENGINE=ncorr USE_GPU=0 THREADS=32 REP=1 \
        submit "E1 t${T} ncorr cpu32" \
            -p keira -c 32 --mem=96G -t 08:00:00 --job-name="xdic-t${T}-ncorr-cpu32"
    fi
    if [ "$ONLY" = all ] || [ "$ONLY" = E2 ]; then
        for R in 1 2; do
            TRIAL=$T ENGINE=cuncorr USE_GPU=1 THREADS=8 REP=$R \
            submit "E2 t${T} cuncorr gpu r${R}" \
                -p gpu --gres=gpu:1 --constraint="TeslaA100|TeslaA100_80" \
                -c 8 --mem=48G -t 12:00:00 --job-name="xdic-t${T}-cuncorr-gpu-r${R}"
        done
    fi
    if [ "$ONLY" = all ] || [ "$ONLY" = E3 ]; then
        TRIAL=$T ENGINE=cuncorr USE_GPU=0 THREADS=8 REP=1 \
        submit "E3 t${T} cuncorr cpu" \
            -p keira -c 8 --mem=48G -t 2-00:00:00 --job-name="xdic-t${T}-cuncorr-cpu"
    fi
done
echo
squeue -u "$USER" -o "%.10i %.28j %.9P %.8T %.10M %.6D %R" | head -20
