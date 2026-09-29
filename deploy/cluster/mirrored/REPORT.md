# Mirrored-camera (MNG) mode — Manneback run report

**Date:** 2026-09-10/11 · **Branch:** `feat/mirrored-mode-mng` (worktree `CPPxDIC-mirrored`,
stacked on `fix/config-driven-pairs-mng-data`) · **Cluster:** Manneback

Three rounds of the mirrored-camera pipeline (`XDIC_MODE=mirrored`, port of
`stepD_2DDIC_MNG.m`) were run on the real 4-camera mirror rig dataset, on two
different node classes each. This report records what now works, how fast it is,
and what still blocks a complete 7-pair reconstruction.

---

## 1. Setup

| Item | Value |
|---|---|
| Data | `mirrored_smoke/data_root` — subject `Zhaochong`, material `index_finger`, trial 001, phase `no_phase` |
| Videos | 4 physical cameras, 1936×1216 @ 50 fps, 837 frames; each frame carries two mirror views side by side |
| Geometry | `mirrored_cam_order = 2,1,4,3`, `mirrored_num_pair = 7` (8 logical views → 7 overlapping stereopairs) |
| REF masks/seeds | present for pairs **1, 3, 5, 7 only** (odd pairs); masks are 1216×968, seeds are single points |
| DIC parameters | radius 25 (tracking) / 30 (matching), spacing 10, `LIMIT_GRAYSCALE` 120, image filtering on, `NO_UPDATE`, `fixed_step_ref = 300` |
| Seed gates | tracking `cutoff_max_corrcoef = 0.5`, matching `matching_cutoff_max_corrcoef = 1.0`, `cutoff_max_diffnorm = 0.1` |
| Engine | CppNCorr (`dic_engine = ncorr`), 32 threads, `parallel_processing = true` |
| Image | `cppxdic_mirrored*.sif`, CPU-only build of `cppxdic_mirrored.def` (`-DXDIC_MODE=mirrored`), ~612 MB |

The two node classes are **keira** (Milan, 32 cores allocated) and the **gpu**
partition (IceLake node, 32 cores allocated). The image contains no CUDA, so the
gpu-partition run is a *node-class* comparison, not a CPU-vs-GPU comparison.

---

## 2. Rounds

| Round | Frames | Change under test | Jobs (cpu / gpu-partition) |
|---|---|---|---|
| v1 | 1..800 | first run of the reworked mode | 9638921 / 9638922 |
| v2 | 1..800 | + matching-seed retry from the ROI centroid, `fixed_step_ref = 300` | 9639293 / 9639294 |
| v3 | 1..700 | shorter window | 9652173 / 9652174 |

---

## 3. Results per stereopair

Identical on both node classes in every round.

| Pair | Views | v1 (800 fr) | v2 (800 fr) | v3 (700 fr) | Why |
|---|---|---|---|---|---|
| 1 | 1 & 2 | matching OK, tracking lost at frame 774 | same | **complete** | late finger motion leaves the fixed reference |
| 2 | 2 & 3 | fail | fail | fail | no REF mask/seed in the dataset |
| 3 | 3 & 4 | fail | fail | fail | matching seed does not converge (user seed **and** ROI centroid) |
| 4 | 4 & 5 | fail | fail | fail | no REF mask/seed |
| 5 | 6 & 5 | **complete** | **complete** | **complete** | — |
| 6 | 7 & 6 | fail | fail | fail | no REF mask/seed |
| 7 | 8 & 7 | **complete** | **complete** | **complete** | — |

**Score: 3 of the 4 pairs that actually have a mask now reconstruct end to end** (v3),
against 0 of 7 in the July 2026 smoke test.

Output of a complete pair (v3): `myDIC2DpairResults_C_<v1>_C_<v2>.mat` plus the three
`ncorr*.bin` caches.

| Pair | Reference points | Delaunay triangles | `myDIC2DpairResults` size |
|---|---|---|---|
| 1 (views 1&2) | 2 105 | 4 146 | 72 MB |
| 5 (views 6&5) | 2 558 | 5 034 | 87 MB |
| 7 (views 8&7) | 3 349 | 6 647 | 114 MB |

---

## 4. Performance (v3, 700 frames, 32 threads)

| Metric | keira (mb-mil015) | gpu partition (mb-icg101) |
|---|---|---|
| Wall clock | 2 h 46 min | 2 h 56 min |
| Peak RSS | 35.7 GB | 34.8 GB |

Per-pair breakdown on keira:

| Phase | Time |
|---|---|
| View extraction (2 views × 700 frames, first pair) | ~3 min |
| view1→view2 matching (2 frames) | ~30 s |
| Tracking view 1 (700 frames) | ~24 min |
| Tracking view 2 (699 frames) | ~24 min |
| **Complete pair total** | **~51 min** |
| Failed pair (extraction + failed matching) | ~3 min |

Round totals were 2 h 44 (v1) and 2 h 39 (v2) at 800 frames against 2 h 46 (v3) at
700 frames: v3 is *slower* despite the shorter window because pair 1 now runs its
tracking to completion instead of aborting at frame 774.

**Determinism.** Every `ncorr*.bin` of the three complete pairs is byte-identical
between the keira run and the gpu-partition run, so the mode is reproducible across
node classes. Outputs are *not* comparable across rounds: an 800-frame and a
700-frame tracking differ from the first reference update onwards.

**Frame cache.** Views are shared between adjacent stereopairs (view 2 serves pairs
1 and 2), and the per-`(trial, view)` PNG cache with its window-signature marker
means each view is decoded once per run. Cost: 6.5 GB per output tree.

---

## 5. What was fixed to get here

The July 2026 smoke test failed on the very first pair with
`matlab_DIC_analysis_sequential could not seed any current image`, and the run died
there. Diagnosis at the time (a missing cross-view seed algorithm) was **wrong**.

1. **Seed-quality gate, not a missing algorithm.** The view1→view2 matching seed
   *converges* (47 iterations, diffnorm 9e-7) at corrcoef **0.51**; CppNCorr's default
   gate `cutoff_max_corrcoef = 0.5` rejected it. Verified offline with `proxyncorr`
   on the extracted view halves. A horizontally flipped view 2 diverges (corrcoef
   1.28), which also proves the mirror views are **not** reversed images.
2. **The gates were never wired.** `cutoff_max_corrcoef`, `cutoff_max_diffnorm` and
   `seeds_are_optimized` were parsed from `ncorr_params.txt` but never copied into
   `DIC_analysis_parallel_input`. Inter-view matching now has its own, looser gate
   (`matching_cutoff_max_corrcoef`, default 1.0) because two mirror views correlate
   worse than two consecutive frames by nature.
3. **One bad pair no longer kills the run.** `processPair()` is wrapped in try/catch,
   so pairs 2, 3, 4 and 6 fail individually and the run continues to 5 and 7.
4. **Engine parity with the camerapairs path.** `runViewDic()` now uses the parallel
   engine when `parallel_processing` is set, honours `NO_UPDATE`, `fixed_step_ref`
   and the seed gates, and reloads existing `.bin` checkpoints.
5. **Trial selection, saturation flags, frame cache.** `run(config, trials)` honours
   `--trial`/`--trials`; saturation honours `im_saturation_mode` / `limit_grayscale`;
   the per-view frame cache avoids re-decoding shared views.
6. **Matching-seed fallback.** If the drawn seed does not converge, the ROI centre of
   mass is tried once (helps the camerapairs rig; does not rescue pair 3 here).

---

## 6. Remaining issues

Ranked by what blocks a full 7-pair reconstruction.

1. **Pairs 2, 4 and 6 have no REF mask or seed** (data, not code). Without them the
   code falls back to a full-frame ROI and a centre seed, which cannot match. The
   MATLAB script prompts the operator to draw them (`draw_ref_roi_MNG.m`,
   `draw_ref_seed_MNG.m`); the C++ path has no interactive drawing, so the four
   missing masks must be produced with the MATLAB tool or `xdic_stepsABC`.
   **This is the single biggest gap: 4 of 7 pairs are simply not set up.**
2. **Pair 3 (views 3 & 4) does not match** even with a converging-seed retry from the
   ROI centroid. Both candidate seeds fail, so either the pair-3 mask covers a region
   the two views do not share, or the seed sits on a poorly textured area. Needs a
   look at the mask against both view halves before any further code change.
3. **Pair 1 tracking is window-limited.** At 800 frames the fixed reference is lost at
   frame 774; at 700 frames it completes. Neither `fixed_step_ref = 300` nor the
   0.5 seed gate rescues the tail. Whoever analyses this trial should either cap the
   window or accept a shorter pair 1.
4. **No Step E for the MNG rig.** `stepE_3DReconstruction_MNG.m` (7 overlapping pairs,
   different stitch topology from the 2-pair camerapairs rig) is not ported, so the
   pipeline stops at 2D pair results. Note that the camerapairs geometric stitcher is
   already pathologically slow on a 4-pair rig, so a 7-pair MNG stitch will need the
   `step_e_stitch_mode = simple` path or an accelerated overlap removal.
5. **Exit code.** The run returns 1 whenever any pair fails, which is correct but means
   a partially successful run looks like a failure to SLURM. Consider a distinct code
   (or a summary line) for "some pairs succeeded".

---

## 7. Reproducing

```bash
# on Manneback, from the synced worktree (no .git; .build_commit carries the SHA)
cd /globalscratch/ucl/inma/jaoga/mirrored_run
sbatch build_mirrored_v3.sbatch                       # ~8 min
MODE=cpu sbatch -p keira        run_mirrored_v3.sbatch
MODE=gpu sbatch -p gpu --gres=gpu:1 run_mirrored_v3.sbatch
```

Locations:

| What | Where (Manneback) |
|---|---|
| Source | `/home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC-mirrored` |
| Image | `mirrored_run/cppxdic_mirrored_v3.sif` |
| Configs | `mirrored_run/configs/` |
| Logs | `mirrored_run/logs/cppxdic_{cpu,gpu}_v3_<jobid>.log` |
| Results | `mirrored_run/output_{cpu,gpu}_v3/Zhaochong/index_finger/001/no_phase/mirrored_pair<N>/` |
| Raw data | `mirrored_smoke/raw/trial1_Zhaochong_index_finger/` |
