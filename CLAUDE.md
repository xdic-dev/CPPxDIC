# CPPxDIC — threading / parallelism model (READ BEFORE TOUCHING THE PROFILING STUDY)

This file exists because a whole thread-scaling study (2026-06-23) was run **sequentially by
accident** — the OMP axis it swept controlled nothing. Don't repeat it.

## How Step-D DIC parallelism ACTUALLY works

CPPxDIC's Step-D tracking calls into the bundled `Tools/CppNCorr` (capital N, capital C).
There are **two completely different parallel paths** in `Tools/CppNCorr/src/ncorr.cpp`, and the
one Step-D uses is NOT the obvious one:

| Path | Function | Parallel over | Thread count source |
|------|----------|---------------|---------------------|
| `DIC_analysis` (classic) | `RGDIC` (ncorr.cpp:1954) | **ROI regions** within ONE frame, via `std::thread` | `DIC_input.num_threads` |
| **matlab path (THIS is what Step-D uses)** | `matlab_DIC_analysis_parallel` → `matlab_run_segment_dic` (ncorr.cpp:4994) | **frames** within a seed-segment, via `#pragma omp parallel for` (each frame run with single-threaded `RGDIC_without_thread`) | `DIC_input.num_threads` |

So in the matlab path the parallel loop is at ncorr.cpp:5047:
```cpp
const difference_type requested_threads = std::min(num_frames, DIC_input.num_threads);
#pragma omp parallel for num_threads(requested_threads) schedule(dynamic)
```

### The two parameters that matter (and the one that DOESN'T)

1. **`parallel_processing`** (`dic_params.txt`) — the MASTER switch.
   - `src/step_d_workflow.cpp:715` passes it as `go_parallel`.
   - `false` → `matlab_DIC_analysis_sequential` (a plain `for` over frames). **No threading at all.**
   - `true`  → `matlab_DIC_analysis_parallel` (the OpenMP frame loop above).
   - **The shipped default in `deploy/cluster/configs/dic_params.txt` is `false`.** This is what
     silently ruined the first study.

2. **`step_d_total_threads`** (`dic_params.txt`) — the actual THREAD COUNT.
   - `config.cpp:233` → `config_.step_d.total_threads` → `step1_params_.total_threads`
     (`step_d_workflow.cpp:294`) → `dic_input` ctor arg #7 (`step_d_workflow.cpp:1239`) →
     `DIC_input.num_threads` → `requested_threads` above.
   - Shipped default `= 1`. With `=1`, even `parallel_processing=true` gives `requested_threads=1`.

3. **`OMP_NUM_THREADS` (env var) — DOES ALMOST NOTHING for Step-D.**
   The frame loop uses `num_threads(requested_threads)`, which **overrides** `OMP_NUM_THREADS`.
   The first study swept `OMP_NUM_THREADS={1,2,4,8,16}` thinking it was the thread axis — it is not.
   (Keep `OMP_NUM_THREADS >= step_d_total_threads` only to avoid a stray nested region
   oversubscribing; `OMP_MAX_ACTIVE_LEVELS=1` already disables nesting.)

### To get REAL parallelism you need ALL of:
- `parallel_processing = true`
- `step_d_total_threads = N`  (N > 1)
- a seed-segment with `num_frames > 1` (segment length, see below)
- `--cpus-per-task >= N`, and set `OMP_NUM_THREADS = N`

## How to PROVE parallelism actually engaged (dispatch log)

When the parallel path runs, `matlab_run_segment_dic` prints to stdout (→ `run.log`):
```
[matlab_run_segment_dic] Parallel dispatch:
  num_frames       = ...
  DIC num_threads  = ...
  Requesting       = N threads
  [segment_dic] frame K on thread T / N took ... s
[matlab_run_segment_dic] per-thread summary:
  thread t: F frames, S s
  wall = ... s, sum-of-threads = ... s, speedup = ... (ideal N)
```
**Sequential runs print `Using Matlab-style sequential DIC processing` and ZERO `Parallel dispatch`
/ `[segment_dic] frame` lines.** Always grep the first run's `run.log` for `Parallel dispatch`
before trusting a scaling sweep.

## Segment length caps the usable thread count

Parallelism is over **frames within a seed-segment** (`matlab_compute_seed_segment`,
ncorr.cpp:4874). A segment runs from a reference frame until a frame fails the seed-quality check;
with correlation-based ref updates disabled (`update_corrcoef = ncorr_cutoff_corrcoef`, set in
`step_d_workflow.cpp:1249`) a clean window is typically ONE segment spanning all its frames.
=> `requested_threads = min(frames_in_segment, step_d_total_threads)`.
A 10-frame window (9 deltas) **cannot exercise more than ~9 threads** — a 16-thread point on a
10-frame window just saturates at 9. To measure N threads, the window needs `> N` frames.

## The profiling-study harness (`deploy/cluster/study/`)

See memory `cppxdic-hpc-profiling-study` and `cppxdic-checkpoint-isolation`. Key points:
- Each experiment gets an **isolated `dic_path`** (`runs/<name>`) seeded with REF/calib symlinks —
  the checkpoint system false-skips otherwise (`step_d_workflow.cpp` + `Utils::buildOutputPath`).
- `run_one.sbatch` appends study overrides to a copy of `configs/dic_params.txt` (last value wins).
  **It MUST append `parallel_processing = true` and set `step_d_total_threads` to the thread axis.**
- Classify rc=139 + `All 2D DIC Analysis completed` + `xprof_phases.csv` as success: Step-F
  (deformation/strain) segfaults downstream; Step-D data is preserved by the XPROF emergency dump
  handler in `include/profiling.h`. Step-F is deferred.
- Array logs: SBATCH `--output` must include `%a` or all array tasks clobber one shared `.out`/`.err`
  (this hid a task-1 failure in the first study).

## `set -euo pipefail` gotcha in run_one.sbatch (cost a whole grid once)
The post-run classify block extracts fields from run.log with grep|...|sort|head pipelines.
Under `pipefail`: a no-match grep returns 1, AND `head -1` SIGPIPEs upstream `sort` (non-zero) —
either aborts the script before `meta.txt` is written, so the run looks FAILED even though the
DIC finished. Every such pipeline MUST end with `|| true` and regexes must match the real log
(`Requesting       = N threads`). If meta.txt is ever missing but run.log+xprof are complete,
recover without recomputing: `deploy/cluster/study/regen_meta.sh` rebuilds meta.txt from
run.log + xprof_phases.csv (parses `/usr/bin/time` "Elapsed (wall clock)" for wall_s).

## RESULT (2026-06-23, corrected grid, jobs 7497464-7497469) — VALID
Tracking parallelizes near-linearly: tracking-loop speedup ~1.0/1.95/3.67/7.1/14.2 at
threads 1/2/4/8/16 (30-frame window, all 3 trials). BUT end-to-end Step-D wall only drops ~1.75x
(e.g. trial7 2961s->1706s) — tracking is only ~46% of Step-D; video import + ROI seeding +
cam-matching are serial (Amdahl ceiling). Peak RSS scales ~linearly with threads:
~5.3G(1) -> 30G(16), ~1.7G/thread (each in-flight frame holds its working set) => 16 threads
needs ~30G RAM. CSV: `analysis/_study/final_parallel_summary.csv`.

## Phase-parallelization experiments (2026-06-23) — verified results
Two optimizations were added behind env switches (default OFF => baseline unchanged), A/B-tested
at trial7/30-frame/8-threads (jobs 7498186-8):
- **#2 `XDIC_PARALLEL_SEEDOPT` — KEEP (big win).** Parallelizes the per-frame seed optimization in
  `matlab_compute_seed_segment` (the serial chunk that capped tracking at ~2.6x). Speculatively
  evaluates all candidate frames, then serial-walks to the first failure to reproduce the EXACT
  MATLAB segment boundary. Result: tracking ~HALVED (442->219, 412->204 s over 2 pairs),
  ~1.35x end-to-end at just 8 threads. **Output bit-identical to baseline** (h5diff on
  ncorr1/ncorr2/MATCHING/myDIC2DpairResults = 0 numerical differences; only the .mat HDF5
  userblock timestamp differs). CAVEAT: speculation evaluates ALL remaining frames per segment,
  so it wastes work when data forms MANY SHORT segments (frequent ref changes). This workflow
  disables correlation-based ref updates (long segments) => speculation is a net win here.
- **#1 `XDIC_PARALLEL_IMPORT` — DROP (no benefit).** Parallel `cv::imread` of frame files gave
  351->344 s (the parallel branch ran; confirmed). `import_video` is I/O-bound on globalscratch,
  not CPU-bound. Code left in place (off) for the record. Numerically safe (h5diff identical).
- Bit-stability MUST be checked with `h5diff` (datasets), NOT md5: .mat v7.3 embeds a creation
  timestamp + dic_path strings, so md5 always differs across runs and means nothing.
- Harness A/B knobs: `PAR_IMPORT`/`PAR_SEEDOPT` in run_one.sbatch -> the env vars; NAME gets a
  `_pi{X}ps{Y}` suffix so A/B runs use isolated dic_paths.

## PAR_SEEDOPT=1 scaling sweep (2026-06-24, jobs 7498361-65) — threads {4,8,16,32,64}, 141-frame window (10-150 j1)
Tracking now scales well (frame loop + parallel seed-opt): internal tracking speedup ~4/7.8/15.3/27/45
at 4/8/16/32/64 threads. BUT end-to-end Step-D PLATEAUS (~trial7 wall 7800/6250/5520/5120/5090 s) —
32≈64 threads. **Sweet spot 16-32 threads; 64 wastes ~2x memory for ~0 gain.** Reason: after
parallelizing tracking, ~77% of a fresh 141-frame run is SERIAL VIDEO I/O:
- `import_video` ~1800 s (the trial's own frames; frame-proportional, I/O-bound on globalscratch).
- `roi_seed_match` ~1875 s — and import_video ~1820 s are BOTH dominated by the same thing:
  **video frame EXTRACTION in `Utils::importRawVid` (utils.cpp:366-397)** — a per-frame
  `VideoCapture.set(CAP_PROP_POS_FRAMES,f); read(); cvtColor; imwrite()` loop that decodes AND writes
  every frame of the source video to disk. `importVid` runs in BOTH phases (import_video extracts the
  trial video; roi_seed_match's `importVideoFrames(reftrial)` extracts the REF video). ~1800 s for
  141 frames x 2 cams. NOTE the per-frame `POS_FRAMES` seek forces keyframe re-decode (slow);
  sequential read would be much faster when frame_jump=1.
- **MISDIAGNOSIS CORRECTED (2026-06-25):** `XDIC_REF_FRAME1` (#1b, import only ref frame 1) was the
  WRONG LAYER — it caps the imread loop in `importVideoFrames`, but `importVid`/`importRawVid` ALREADY
  extracted+wrote all 141 frames before that loop, so it saved only the fast re-read (~25 s, roi_seed
  1876->1851). Bit-identical (h5diff verified) but ineffective. Verified by the 2026-06-25 rerun
  (jobs 7499274-78; thr32/64 cancelled once the no-gain was clear).
- REAL fixes (NOT yet implemented): (1) **frame-extraction cache** — skip decode+imwrite if the frame
  file already exists; the REF trial (e.g. 005) is identical across ALL target trials so it is
  re-extracted per trial today -> cache it once. (2) **cap the EXTRACTION** (frameEnd=frameStart) for
  the ref match by plumbing the limit into importVid/importRawVid, not importVideoFrames. (3)
  sequential read instead of per-frame seek. roi_seed_match & matching_cams are ALSO checkpoint-cached
  (skipped if base_params_.matchingfile exists), so in production warm-cache they ~vanish.
Memory ~1.7 GB/thread: 11.9/18.5/31.9/58.6/111 GB at 4/8/16/32/64 threads. CSV:
`analysis/_study/seedopt_sweep_summary.csv`. Baseline (seedopt off, 30-frame) archived at
`_archive_20260623_195106_baseline_seedopt_off/`.

## Rebased onto main (2026-06-25): upstream PR #35 fixed the import properly
ja/profiling was rebased onto origin/main (now has PRs #31 webgui, #32 staged --stages pipeline,
#35 import perf, #36 optional apps). **PR #35 (`perf: fix slow video import`) is the real fix**:
seek-once + sequential read (no per-frame POS_FRAMES re-decode) AND a maxFrames cap on
importVid/importRawVid/importVideoFrames (1 frame for the REF match) — i.e. my #1/#1b done at the
right layer. **My #1 (XDIC_PARALLEL_IMPORT) and #1b (XDIC_REF_FRAME1) were DROPPED** (superseded;
gone from src/). Kept on top of main: XPROF instrumentation (merged into the new `plan`-based
runStepD / staged STEP_D/E/F), force-frames patch (utils.cpp, coexists with #35), XPROF crash
handler (profiling.h), and **seed-opt #2 (XDIC_PARALLEL_SEEDOPT) in the submodule** (main doesn't
touch Tools/CppNCorr). Harness simplified to a single PAR_SEEDOPT knob (name suffix `_ps1`).
Rebase conflict resolution: combined XPROF scopes with main's `if(plan.track/match/format)` and
`dic2DAnalysis(trial_target, plan)` signature. Merged binary compiles + self-tests clean.

**Validation (merged binary, 30-frame, thr8, seedopt on):** import_video ~338->**15 s** (~22x via
#35), roi_seed_match ~412->**50 s** (~8x), STEP_D total **759 s**, seedopt parallel_ok=1 speedup 7.2,
output rc=139 (expected Step-F crash). The import bottleneck is GONE. Clean rerun: jobs 7502692-96
(threads 4-64, 10-150 j1, PAR_SEEDOPT=1, names `_ps1`). CSV: `analysis/_study/merged_sweep_summary.csv`.

**FINAL merged-sweep result (141-frame, per-phase trial7, 2 pairs):** import_video 60s FLAT (was
~1820 -> ~30x), roi_seed_match ~100s FLAT (was ~1850 -> ~18x), matching_cams ~185s FLAT. Tracking
now dominates and scales to ~32 threads then saturates (tracking1 thr4..64: 1955/1220/875/710/773 —
REGRESSES at 64). End-to-end Step-D wall (trial7): 4225/2761/2041/1666/1671 s at 4/8/16/32/64 thr.
**Sweet spot = 32 threads (~1650s); 64 gives ~0 gain.** vs the pre-merge seedopt sweep (slow import)
this is ~3.1x faster at 32 threads (5175->1666). Remaining serial floor ~345s (import+roi_seed+match)
is checkpoint-cached in production. Net: original sequential Step-D ~7800s (141-frame) -> ~1650s.

## Optional-apps tests (2026-06-26) + a real concurrency bug fix
Harness: `deploy/cluster/study/apps/` (setup_isolated.sh, trial_batch.sbatch, singledic.sbatch,
stage.sbatch + staged_run.sh, proxyncorr.sbatch). SIF must be built with
`-DBUILD_SINGLEDIC=ON -DBUILD_PROXYNCORR=ON -DBUILD_GEN_SUBJECT_TRIAL=ON` (done in cppxdic_prof.def).
All tests use S09, 141-frame window (10-150), 32 threads. S09 has trials 5,7,12,25 (NO trial 15).

- **BUG FIXED (concurrency):** `runNcorrAnalysis` wrote `tmp_ncorr_images/{ref,cur_*,roi_mask}.png`
  with FIXED names under the shared `dic_path/subj/material/trial/phase` dir (step_d_workflow.cpp
  ~1244), so concurrent per-camera / per-pair staged tasks on one dic_path corrupted each other's
  PNGs (libpng read errors). Fix: temp dir is now `tmp_ncorr_<output-stem>` (unique per camera/pair,
  since output filenames encode them). This is what makes the cam-granularity staged decomposition
  safe; it also fixes the same latent race in main's deploy/cluster/staged scripts.
- **trial-batch** (`--trials 7,12,25`, array 1/trial): WORKS. Gotcha: `apptainer --cleanenv` STRIPS
  `SLURM_ARRAY_TASK_ID` — pass it explicitly or every task runs the whole list. 3/3 trials, ~28 min.
- **staged** (cam granularity: match/(trial,pair) -> track/(trial,pair,cam)@32 -> ef/(trial)): WORKS
  end-to-end after the fix — 6 match + 12 concurrent track (~7 min each) + 3 ef, producing per-cam
  ncorr1-4.bin + DIC3Dcombined per trial.
- **singledic** (1 camera): WORKS + parallelizes (RGDIC num_threads=32) but is ~280 s/FRAME (classic
  spatial-threaded RGDIC, no frame-parallel/seed-opt) -> 141 frames ≈ 11 h, TIMEOUT at 6 h. Validated
  at small windows. Needs <data_path>/vid/<subject>/<bloc>/<trial>.mp4 layout (symlink one cam video).
- **proxyncorr**: REF files are MATLAB-5 (unreadable on login: no scipy/matio/ffmpeg/convert). The
  pipeline itself renders REF_MASK -> roi_mask.png (step_d_workflow.cpp:1266) and logs REF_SEED
  ("Provided seed: X Y"), so we feed those. The seed-based `--mode parallel --seeds` path SEGFAULTS
  (proxyncorr bug, ExitCode 11, valid 1936x1216 ROI); use `--in-memory` (NcorrSession) with the
  REF-mask ROI instead. Frames come from the pipeline's own tmp_frames/T<trial>/.../frame_*.png.

## The 2026-06-23 *first* sequential study is ARCHIVED as invalid
Under `analysis/_study/_archive_<ts>_sequential_invalid/`. Its OMP sweep measured nothing; wall
time was flat across OMP {1,2,4,8,16} precisely because every run was sequential. Do not use it for
scaling conclusions.
