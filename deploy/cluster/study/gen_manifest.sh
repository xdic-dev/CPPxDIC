#!/bin/bash
# =============================================================================
# Generate the study grid manifests (data-only rows, no header):
#   columns:  trial fstart fend fjump threads par rep
#
#   threads = step_d_total_threads = the REAL Step-D thread axis. In CppNCorr's
#             matlab path this is requested_threads of the OpenMP *frame* loop
#             (min(frames_in_segment, threads)). OMP_NUM_THREADS does NOT drive
#             it — see CPPxDIC/CLAUDE.md. run_one sets OMP_NUM_THREADS=threads
#             and --cpus-per-task=threads to match.
#   par     = parallel_processing flag (1 = parallel matlab path, 0 = sequential).
#             MUST be 1 or the thread axis is a no-op (the 2026-06-23 bug).
#
# Two parts (separate files so submit_grid.sh can size each tightly):
#   manifestA.txt  THREAD SWEEP   — scaling curve: vary threads on a fixed window
#   manifestB.txt  FRAME STUDY    — fixed threads=8: vary window size/density/pos
#
# Thread sweep uses a 30-frame window (10-39) so frames(29) > max threads(16):
# every point {1,2,4,8,16} has frames>threads and is a real data point.
# =============================================================================
cd "$(dirname "$0")"

TRIALS=(7 12 25)
THREADS=(4 8 16 32 64)
REPS=2

# --- (A) THREAD SWEEP: 141-frame window (10-150, jump 1), parallel ON ---
# Window has 140 deltas >> max threads (64), so every point {4,8,16,32,64} has
# frames > threads and is a real data point (both the frame-DIC loop and the
# seed-opt cap at min(frames, threads)). Run with PAR_SEEDOPT=1 (see submit_grid).
: > manifestA.txt
for t in "${TRIALS[@]}"; do
  for r in $(seq 1 $REPS); do
    for n in "${THREADS[@]}"; do
      echo "$t 10 150 1 $n 1 $r" >> manifestA.txt
    done
  done
done

# --- (B) FRAME STUDY: omitted for this rerun. The dense full-window case is now
#     subsumed by the thread sweep (10-150 j1) at threads=8/16/.... Re-enable if a
#     window-shape study (sparse jump, late frames) is needed again. ---
: > manifestB.txt

echo "manifestA (thread sweep): $(wc -l < manifestA.txt) jobs"
echo "manifestB (frame study):  $(wc -l < manifestB.txt) jobs"
echo "total: $(( $(wc -l < manifestA.txt) + $(wc -l < manifestB.txt) )) jobs"
