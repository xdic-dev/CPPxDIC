# External dataset "Artem" (8-view MNG rig) — diagnosis, fixes and CPU/GPU runs

**Date:** 2026-09-10 → 2026-09-13 · **Branch:** `fix/config-driven-pairs-mng-data`
(8 commits on top of `main`) · **Clusters:** Lemaitre4, Manneback

An external user could not get `cppxdic` to run on an 8-view / 4-stereopair rig and
reported three symptoms: *(a)* "my modified parameters are not taken into account",
*(b)* "the video is already filtered, no filtering or saturation is needed",
*(c)* "some pairs are not processed". This report records what actually caused each
symptom, what was changed, and the results of the full-pipeline runs that followed.

---

## 1. Dataset

| Item | Value |
|---|---|
| Location (Lemaitre) | `/globalscratch/ucl/inma/jaoga/louis/CPPxDIC_Data` (+ `CPPxDIC_John`, the user's copy of `main`) |
| Subject / material / phase | `Artem` / `glass` (`material_id = 1`, **1-based**) / `loading` |
| Rig | 8 views → 4 stereopairs, `camera_pairs = 1,2;3,4;6,5;8,7`, calibration set 1 (`DLTstruct_cam_1..8`) |
| Videos | already split per view and pre-filtered upstream, 968×1216 @ 50 fps |
| Trials | 001 (837 frames), 002 (791), 003 (916), … 012 — 12 trials, reference trial 001 |
| Naming | only trial 001 follows `<subj>_<mat>_speckles_<trial>_…`; 002-012 keep the raw `Artem_index_block1_0NN_…_cam_K.mp4` |
| Protocol | MNG `.tsv` (not parsed by cppxdic); the user had copied an unrelated S09 `.mat` into `protocol/` |
| REF masks/seeds | present for all 4 pairs |

Because the views are already split and filtered, this rig runs in the **camerapairs**
mode, not the mirrored mode — the mirror slicing was done upstream.

---

## 2. Root causes

### (a) "My parameters are not taken into account"

**`camera_pairs` was parsed and then ignored.** Every one of the ten
`Utils::getCamerasForPair()` call sites used a hard-coded `{1,2 ; 4,3}` table, so on a
4-pair rig pairs 3 and 4 silently resolved to cameras (1, 2). The pair-3 reference mask
was therefore applied to camera 1's images.

**The frame window was overridden silently.** Any `*.mat` in the protocol directory sets
the per-phase window, so the stray S09 protocol clamped every trial to 150 frames
regardless of `idx_frame_end`.

**A missing protocol was fatal.** With an MNG `.tsv` protocol and no `.mat`, the run
aborted in `checkDataAndProtocol` even though `ref_trial_id` makes the protocol
unnecessary.

### (b) "No filtering / saturation needed"

`im_filter_mode = false` already skipped the bandpass filter, but **saturation was
unconditional**. Worse, the clipping level came from the *subject number* (`S01..S07 → 70`,
`S08+ → 100`); `"Artem"` contains no digits, so it counted as 0 and got 70. Clipping
already-normalised frames at 70 flattens the speckle contrast, which is exactly the
`d_newton: hessian failed` divergence in the user's logs.

### (c) "Some pairs are not processed"

Same cause as (a): pair 3 ran camera 1's frames against pair 3's mask and failed with
`matlab_DIC_analysis_sequential could not seed any current image`.

### Also found

- `video_quality = high` (the value shipped in `visualization_params.txt`) hit a bare
  `std::stoi` and killed the process before any DIC work started.
- `cutoff_max_corrcoef`, `cutoff_max_diffnorm` and `seeds_are_optimized` were parsed from
  `ncorr_params.txt` but never copied into `DIC_analysis_parallel_input`, so CppNCorr's
  built-in 0.5 / 0.1 always applied.
- The user was running a **June profiling image** built from a different branch.

---

## 3. Changes

| Commit | Change |
|---|---|
| `bdc9db7` | process-wide stereopair→cameras registry fed from `camera_pairs`; `im_saturation_mode`; `limit_grayscale = 0` means auto; `force_frame_window`; protocol optional when `ref_trial_id` is set; relaxed video-name fallback; `video_quality` accepts `high/medium/low`; startup log prints resolved pairs, saturation, engine and threads; `TRIAL`/`TRIALS`/`APP_ARGS` in `submit_job.sh` |
| `825e546` | seed-quality gates actually reach the engine; separate `matching_cutoff_max_corrcoef` (default 1.0) for inter-camera matching |
| `054f4ac` | retry the matching seed from the ROI centre of mass when the drawn seed does not converge |
| `ec1fc14` | transport the second camera's seed only through a **valid** grid point of the matching field |
| `2e2a8ae` | cluster template: tracking seed gate back to the engine default 0.5 |
| `2f8f9da` | `step_e_stitch_mode = geometric \| simple` |
| `a88a5b9` | optional AABB prefilter for the stitcher ray casts (`step_e_ray_bvh`, default off) |
| `ca29de8` | CLAUDE.md: the silently-ignored keys, both MNG datasets, the mirrored worktree |

### The change that actually unblocked the data

The user's own `configs.zip` carried `camera_pairs = 1,2;3,4;**6,5;8,7**` — pairs 3 and 4
**reversed**. That ordering decides which camera is the reference, hence which REF
mask/seed applies. With it, **all four pairs run at full length** and the seed
workarounds (`054f4ac`, `ec1fc14`) are never needed. The earlier `5,6;7,8` ordering was
wrong for this rig; the same reversed convention already worked in the mirrored mode's
view pairs (6&5, 8&7). Lesson: on these rigs the pair ordering is part of the
calibration, not a cosmetic detail.

---

## 4. Configuration used for the runs

The user's `configs.zip` verbatim, plus `step_e_stitch_mode = simple` on both clusters
(a precaution taken when geometric stitching was believed to be unusably slow — §6 shows
it would have been affordable). The only intentional difference between the two runs is
`dic_engine`.

```
camera_pairs = 1,2;3,4;6,5;8,7      num_pair = 4        ref_trial_id = 1
idx_frame_start/end = 1 / 1000      force_frame_window = true
im_filter_mode = false              im_saturation_mode = false    limit_grayscale = 0
step_d_radius = 40  spacing = 10    step_*_total_threads = 32     parallel_processing = true
cutoff_corrcoef = 1.0   cutoff_max_corrcoef = 1.0   matching_cutoff_max_corrcoef = 1.0
dic_config = KEEP_MOST_POINTS       step_e_stitch_mode = simple
dic_engine = cuncorr (GPU run) | ncorr (CPU run)
```

---

## 5. Results — all 12 trials, full pipeline (D + E + F)

Both engines on Manneback, 32 cores, reference trial 001. GPU = cuNCorr on A100
(40 or 80 GB), CPU = ncorr on 32 threads (keira). **23 of 24 runs succeeded** with all
4 pairs; the one failure is CPU trial 011 (below).

| Trial | Frames | Wall GPU | Wall CPU | Step D GPU | Step D CPU | D speedup | Points | Faces | RSS GPU / CPU |
|---|---|---|---|---|---|---|---|---|---|
| 001 | 837 | 6:28 | 7:29 | 19 774 s | 22 715 s | 1.15× | 11 834 | 22 888 | 77 / 114 GB |
| 002 | 791 | 6:35 | 7:51 | 18 777 s | 23 535 s | 1.25× | 11 790 | 22 801 | 73 / 102 GB |
| 003 | 916 | 7:19 | 8:55 | 22 294 s | 26 014 s | 1.17× | 11 837 | 22 892 | 85 / 110 GB |
| 004 | 674 | 4:57 | 6:04 | 15 779 s | 18 987 s | 1.20× | 11 919 / 11 934 | 23 053 / 23 083 | 63 / 89 GB |
| 005 | 782 | 5:34 | 6:19 | 17 180 s | 19 516 s | 1.14× | 11 781 / 11 782 | 22 779 / 22 781 | 72 / 98 GB |
| 006 | 824 | 6:09 | 6:52 | 18 918 s | 21 089 s | 1.11× | 11 932 / 11 947 | 23 075 / 23 105 | 77 / 101 GB |
| 007 | 737 | 5:35 | 6:01 | 17 613 s | 18 838 s | 1.07× | 11 924 | 23 065 | 69 / 93 GB |
| 008 | 774 | 5:45 | 6:26 | 17 899 s | 19 905 s | 1.11× | 11 925 | 23 067 | 72 / 97 GB |
| 009 | 786 | 5:55 | 6:43 | 18 389 s | 20 768 s | 1.13× | 11 861 | 22 941 | 73 / 103 GB |
| 010 | 693 | 5:10 | 5:36 | 16 362 s | 17 556 s | 1.07× | 11 906 | 23 031 | 65 / 95 GB |
| 011 | 711 | 5:19 | **failed** | 16 763 s | — | — | 11 901 | 23 019 | 66 / 29 GB |
| 012 | 1000 | 7:48 | 9:27 | 22 783 s | 27 785 s | 1.22× | 11 907 | 23 033 | 93 / 117 GB |

Every trial reconstructs ~11.8-11.9 k points and ~23 k faces over 4 pairs, and both
engines agree exactly on 9 of 11 comparable trials (trials 004, 005 and 006 differ by
1-15 points out of ~11 900, i.e. slightly different valid-point sets at the ROI edges).
Each run writes `DIC3Dcombined_4Pairs_stitched.mat` and `DIC3DPPresults_4Pairs_cum_v2.mat`.

**Step D speedup: 1.07-1.25×, mean ≈ 1.15×.** Peak memory is consistently lower on the
GPU path (63-93 GB vs 89-117 GB) because ncorr keeps 32 frames in flight.

**The one failure — CPU trial 011**, pair 2: `matlab_DIC_analysis_parallel could not seed
the segment starting at reference frame 683` (of 711). The GPU run of the same trial with
the same configuration completed. This is the late-trial tracking loss of §7.4: ncorr
re-references in segments and the delayed re-reference could not be seeded, whereas
cuNCorr tracks from a fixed reference with a warm start and does not re-seed. Worth
noting as an engine robustness difference, not a data problem.

### Phase breakdown, trial 001 (from output-file timestamps)

| Phase (per pair) | cuNCorr GPU | ncorr 32 threads |
|---|---|---|
| REF→trial matching + inter-camera matching | ~35 s | ~40 s |
| Tracking camera 1 | 37-38 min | 42-45 min |
| Tracking camera 2 | 38-44 min | 44-47 min |
| `step2_dic_finish` formatting | ~2.5 min | ~3.7 min |
| **Tracking, all 8 cameras** | **5 h 11** | **5 h 56** |

Tracking is **94 %** of Step D, so the end-to-end figures are not diluted by import or
formatting overhead: the GPU really is only **~1.14×** faster than 32-thread ncorr on
this dataset.

### The GPU speedup is far below the earlier study — unexplained

The S09 study (`deploy/cluster/cpugpu/results/REPORT.md`) measured **3.7-4.2×** for
cuNCorr/A100 against ncorr on 32 threads. Here it is 1.14× on the correlation itself.
Per camera per frame: GPU 2.7 s here vs 1.7 s on S09; CPU 2.98 s here vs 6.3 s on S09 —
i.e. the CPU path got much faster and the GPU path slightly slower.

Checked and ruled out:

- **engine dispatch** — verified in the logs: 8 cuNCorr tracking calls + 8 ncorr
  sequential matching calls per trial on the GPU run, 8 ncorr-parallel + 8
  ncorr-sequential on the CPU run. Matching is correctly kept on ncorr.
- **reference updates defeating the GPU warm start** — `no_update` defaults to true
  (NO_UPDATE preset) and `update_corrcoef = 1.0` is far above the observed 0.02-0.05, so
  no correlation-based re-referencing happens in either run.

Still open: ROI size (~2 900 points here vs 3 646 on S09) may leave the GPU
under-occupied per frame, and this branch carries CppNCorr fixes the S09 branch lacked.
**Resolving this needs one instrumented run** (`XDIC_PROFILE`-style timing inside the
tracking loop, or a single-pair run at several ROI sizes). Until then, treat 1.14× as the
measured figure for this rig and 3.7-4× as specific to S09.

---

## 6. Step-E stitching study (2026-09-12/13)

I previously reported that geometric stitching was "pathologically slow, > 9 h on a
4-pair rig" and implemented an AABB prefilter for the ray casts to fix it
(`step_e_ray_bvh`, opt-in, default off). Measuring it properly **did not support that
claim** and the record is corrected here.

### The prefilter is exact

Three input sets, each stitched twice (prefilter off / on), same Step E otherwise:

| Input (trial 001, 4 pairs) | Off | On | Ratio | Stitched mesh (both) |
|---|---|---|---|---|
| cuNCorr 2D results, correct pairing | 699 s | 555 s | 1.26× | 11 834 pts / 12 605 faces |
| ncorr 2D results, correct pairing | 741 s | 603 s | 1.23× | 11 834 pts / 12 605 faces |
| ncorr 2D results, old pairing (780 fr) | 551 s | 496 s | 1.11× | 11 834 pts / 12 960 faces |

Identical point and face counts every time, and a dataset-level `h5diff` of the two
`DIC3Dcombined` files reports **0 differences** on every comparable object (the
"not comparable" entries are v7.3 struct/cell layouts, not values). The two engines'
2D results also stitch to exactly the same mesh, which cross-checks the reconstruction
path itself.

### The speedup is modest, and the "> 9 h" case did not reproduce

Ray casting drops from roughly 150 s to a few seconds, but Step E on this rig is
dominated by per-pair reconstruction, triangulation and writing a 2.7 GB output, so
end-to-end it is only 1.1-1.3×.

More importantly: the *same* 2D results from the Lemaitre job that I killed after five
hours in stitching (`Results_t1_780`, old pairing) complete Step E in **9 min 13 s on
Manneback with the prefilter off** — the identical code path. So the hours-long stitching
observed on Lemaitre is **not** explained by the stitching algorithm, by the rig having
four pairs, or by the camera-pair ordering. Its cause is unknown; the most likely
candidate is the state of that cluster (the batch partition was saturated, 40/40 nodes
allocated with ~12 000 jobs queued), but the jobs are gone and it could not be
re-measured. **Do not treat "geometric stitching is unusable on multi-pair rigs" as
established.**

Consequences:

- `step_e_ray_bvh` stays **off by default**. It is exact and slightly faster, but it is
  not the rescue it was introduced to be, and an off-by-default option carries no risk.
- `step_e_stitch_mode = simple` was used for the production trials above as a
  precaution. On this evidence `geometric` would have been affordable (~10 min).

### Separate finding: geometric stitching can silently discard every face

On the 2-pair S09 inputs (`cpugpu/runs/t7_cuncorr_gpu_r3`), Step E run on its own
produces **6 738 points and 0 faces**: the stitcher's "remove NaN faces" step drops every
face whose vertices are NaN in frame 0, overlap removal then returns immediately, and the
result is written and reported as a success. The July image built from `7a0e3ce` produces
the identical 0-face output on the same input, so this predates the branch. It has not
been diagnosed further. A stitch that loses all faces should fail loudly rather than
write a face-less mesh.

## 7. Remaining issues

1. **The two findings above**: the unexplained Lemaitre slowness, and the silent
   0-face stitch on S09 inputs.
2. **Step F cost grows steeply with point count** — 51-87 min for ~11.8 k points over
   ~800 frames, against minutes on the 2-pair rig. Worth profiling.
3. **GPU speedup far below the S09 study** (§5): 1.14× on tracking vs 3.7-4×. Engine
   dispatch and reference updates ruled out; needs an instrumented run.
4. **Trial-window sensitivity.** With the old pair ordering, camera-6 tracking was lost
   at frame 792/837 (trial 001) and 868/916 (trial 003). The reversed ordering removed the
   symptom, but a trial with strong late motion can still exhaust a fixed reference.
5. **Lemaitre4 queue.** The 12-trial CPU rerun (jobs 7786071-82) is at position ~11 300 of
   ~12 000 pending, estimated start 2026-09-20. Manneback is the practical place to run this.
6. **Peak memory.** ncorr on 32 threads reaches 102-114 GB on 837-916 frame trials;
   cuNCorr stays at 73-85 GB. Jobs need `--mem=120G`.
7. **Interactive prerequisites.** Nothing in the C++ path draws reference masks or seeds;
   they must come from MATLAB or `xdic_stepsABC`.

## 8. Reproducing

```bash
# Manneback — GPU (cuNCorr) and CPU (ncorr) share data, configs and Results trees
cd /globalscratch/ucl/inma/jaoga/louis_run
sbatch build_gpu_sif.sbatch                                   # ~25 min
TRIAL=1 sbatch -p gpu --gres=gpu:1 \
  --constraint='TeslaA100|TeslaA100_80' run_trial_gpu.sbatch   # -> Results/
TRIAL=1 sbatch -p keira            run_trial_cpu.sbatch        # -> Results_cpu/
```

| What | Where |
|---|---|
| Source (Manneback, no `.git`; `.build_commit` has the SHA) | `/home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC-fix` |
| Image | `louis_run/cppxdic_gpu.sif` |
| Configs | `louis_run/configs_v2` (GPU) · `louis_run/configs_v2_cpu` (CPU) |
| Results | `louis_run/Results` (GPU) · `louis_run/Results_cpu` (CPU) |
| Logs | `louis_run/logs/artem{gpu,cpu}-t<N>_<jobid>.{out,err}` |
| Original data (Lemaitre) | `/globalscratch/ucl/inma/jaoga/louis/CPPxDIC_Data` |
| Earlier Lemaitre attempts (backed up) | `louis_run/backup_20260910/`, `louis_run/backup_20260911_round4/` |
