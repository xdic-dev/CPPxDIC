# External dataset "Artem" (8-view MNG rig) — diagnosis, fixes and CPU/GPU runs

**Date:** 2026-09-10 → 2026-09-12 · **Branch:** `fix/config-driven-pairs-mng-data`
(7 commits on top of `main`, 14 files, +445/−64) · **Clusters:** Lemaitre4, Manneback

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
(the geometric stitcher is unusably slow here, see §6). The only intentional difference
between the two runs is `dic_engine`.

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

## 5. Results — trials 001-003, full pipeline (D + E + F)

Both engines on Manneback, same node class, 32 cores, `rc = 0`, all 4 pairs.

| Trial | Frames | Engine | Step D | Step E | Step F | Wall | Peak RSS |
|---|---|---|---|---|---|---|---|
| 001 | 837 | cuNCorr / A100 80GB | 5 h 29 | 7.6 min | 51 min | **6 h 28** | 78 GB |
| 001 | 837 | ncorr / 32 threads | 6 h 19 | 9.5 min | 60 min | **7 h 29** | 114 GB |
| 002 | 791 | cuNCorr / A100 40GB | 5 h 13 | 11.8 min | 70 min | **6 h 35** | 73 GB |
| 002 | 791 | ncorr / 32 threads | 6 h 32 | 10.6 min | 68 min | **7 h 51** | 102 GB |
| 003 | 916 | cuNCorr / A100 80GB | 6 h 12 | 9.2 min | 59 min | **7 h 19** | 85 GB |
| 003 | 916 | ncorr / 32 threads | 7 h 14 | 14.2 min | 87 min | **8 h 55** | 111 GB |

Reconstruction is identical between the two engines:

| Trial | Frames | Points | Faces | Per-pair points |
|---|---|---|---|---|
| 001 | 837 | 11 834 | 22 888 | 2934 / 2699 / 2838 / 3363 |
| 002 | 791 | 11 790 | 22 801 | 2923 / 2695 / 2828 / 3344 |
| 003 | 916 | 11 837 | 22 892 | 2941 / 2712 / 2846 / 3338 |

Each trial writes `DIC3Dcombined_4Pairs_stitched.mat` and
`DIC3DPPresults_4Pairs_cum_v2.mat`. Trials 004-012 are running (both engines).

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

## 6. Remaining issues

1. **Geometric stitching is unusable on a 4-pair rig.** `stitchPairsGeometric` removes one
   vertex per iteration and re-ray-traces every boundary vertex against every face of the
   other surface, with no spatial acceleration. On the 2-pair S09 rig it takes minutes; on
   this rig two trials ran **> 9 h** in stitching alone and were killed. All results above
   use `step_e_stitch_mode = simple` (plain append, seconds). Fix: an AABB/grid prefilter
   in `raySetTriangleIntersect` (exact same results, orders of magnitude faster) and/or
   removing a whole erosion front per iteration instead of one vertex.
2. **Step F cost grows steeply with point count.** 51-87 min for ~11.8 k points over
   ~800 frames, against minutes on the 2-pair rig. Worth profiling before this rig is used
   at production scale.
3. **Trial-window sensitivity.** With the old (wrong) pair ordering, tracking of camera 6
   was lost at frame 792/837 (trial 001) and 868/916 (trial 003) — the finger unloads and
   moves in the last ~45 frames. The reversed ordering removed the symptom for these
   trials, but any trial with strong late motion can still exhaust a fixed reference; a
   `fixed_step_ref` sweep would be the mitigation.
4. **Lemaitre4 queue.** The 12-trial CPU rerun (jobs 7786071-82) sits at position ~11 300
   of ~12 000 pending with an estimated start of 2026-09-20; that partition had 40/40
   nodes allocated. Manneback (keira + gpu) is the practical place to run this workload.
5. **Peak memory.** ncorr on 32 threads reaches 102-114 GB on 837-916 frame trials
   (32 frames in flight); cuNCorr stays at 73-85 GB. Jobs need `--mem=120G`.
6. **Interactive prerequisites.** Nothing in the C++ path draws reference masks or seeds;
   they must come from MATLAB or `xdic_stepsABC`. Worth stating in the user-facing docs.

---

## 7. Reproducing

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
