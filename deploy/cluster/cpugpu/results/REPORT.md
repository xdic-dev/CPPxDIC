# CPU vs GPU comparison — results (2026-07-04)

Manneback, branch `feat/cuncorr-integration`, image `cppxdic_gpu.sif` (single CUDA
build, sm70-89; identical binary for all runs). S09/coating trials 7/12/25, Step D
only (`--stages d`), protocol-derived loading window. Jobs 7863613-15, 7863691-99.
All 12 runs COMPLETED, `status=ok`, engine dispatch verified in run.log
(GPU runs: `cuNCorr backend: cuda:0 NVIDIA A100-PCIE-40GB (CUDA)`; E3 runs: `cpu (CPU)`).

## Performance (wall, full Step D incl. import/matching)

| trial | E1 ncorr CPU 32thr | E2 cuncorr GPU (A100) | E3 cuncorr CPU 1thr |
|-------|-----------------|----------------------|---------------------|
| 7     | 3784 s          | **1021 / 1020 s**    | 5866 s              |
| 12    | 3832 s          | **1027 / 1027 s**    | 5791 s              |
| 25    | 4592 s          | **1083 / 1083 s**    | 7175 s              |

Peak RSS: E1 ~54-60 GB (32 in-flight frames), E2 ~3.7 GB, E3 ~5.9 GB.
GPU ≈ 3.7-4.2x faster than the 32-core baseline at ~15x less memory.
Single-thread cuncorr-CPU ≈ 1.5x slower than 32-thread ncorr — the fixed-ref
warm-start algorithm is intrinsically much cheaper than ncorr's seed-segment scheme.

## Bit-comparison (h5diff on .mat datasets, cmp on .bin; never md5)

1. **GPU determinism (E2 r1 vs r2)**: all numeric outputs (ncorr1-4, ncorr12/43,
   MATCHING, myDIC2DpairResults, .bin+.mat) **bit-identical**. Only
   `dic_info_data_target_pair*.mat` flag differences (non-comparable string
   metadata: embedded dic_path). Wall times matched to the second.
2. **CPU vs GPU, same engine (E3 vs E2)** — the meaningful "CPU vs GPU" axis:
   cams 2+3 tracking and both matching DICs (ncorr12/43) **bit-identical**;
   cams 1+4 (and derived MATCHING / myDIC2DpairResults) differ in many elements
   but **zero elements differ by more than 1e-12, all trials** — pure ULP-level
   FP noise (GPU FMA/rounding) that occasionally flips and propagates through
   ICGN iteration counts. Numerically equivalent for every practical purpose.
3. **Engine (E1 ncorr vs E2 cuncorr)**: NOT bit-comparable by design — the
   engines keep different valid-point sets (e.g. t7 pair1: 3646 vs 3655 points,
   ~0.25% at ROI edges), so most per-frame datasets are dimension-mismatched,
   and intermediate ncorr*.mat use different displacement conventions
   (segment-relative vs fixed-reference). Judging engine agreement needs a
   field-level analysis (interpolate both onto a common grid), not h5diff.

Full log: `compare_all.txt` (this dir). Raw outputs: `/globalscratch/ucl/inma/jaoga/cpugpu/runs/`
(purgeable — this dir on home is the durable record).

## MATLAB reference comparison (added 2026-07-04, trial 007)

Original MATLAB MultiDIC results (Nov 2025 run, `matlab_ref/loading/`) compared with
the matio-based `compare_dic` tool (POINTS mode: rows matched by frame-1 grid
coordinates; MATLAB .mat v5 + struct-array layouts are unreadable/misaligned for
h5diff). Full logs: `matlab_comparison/`, `points_summary.txt`; jobs 7878716/7878834.

- **Setup parity**: identical radius/spacing/cutoffs (40/10/1e-5/100), identical
  111x176 grid, 150 frames, and near-identical valid point sets vs E1
  (pair1: 3646 of 3651 MATLAB points, 0 extra).
- **Trajectories vs E1 (ncorr CPU)**: ~86% of matched coordinate samples within
  0.1 px, but a real tail (max 55 px, corrcoef mismatch up to 1.4).
  **Root cause identified, not a port bug**: MATLAB ran with per-frame
  correlation-based reference updates (dispinfo.cutoff_corrcoef = 150 per-frame
  values ~0.02-0.05) while cppxdic intentionally disables them
  (update_corrcoef=10, the documented divergence fix); cppxdic also ran the
  `matlab_*` path, not `exact_matlab_*`. Also `pixtounits` metadata differs
  (MATLAB 1 vs cppxdic 0.2) — worth checking which is intended downstream.
- **Trajectories vs E2 (cuncorr GPU)**: same-magnitude agreement (94% of matched
  samples within 0.1 px, max ~5.5 px); E1-vs-E2 final products likewise
  (max ~10 px tail at hard points). Caveat: matching had to fall back to
  frame-2 coords at 0.5 px buckets (see below), so ~20% of points went unmatched
  and the tail estimates are approximate.
- **cuNCorr adapter bug found**: `myDIC2DpairResults` Points{1} (the reference-
  frame seed grid) is ALL NaN in cuncorr runs — ncorr/MATLAB store the exact grid
  there. Breaks exact point matching and may affect Step-E's first frame. Fix in
  the adapter/format_output path.

Bottom line: MATLAB vs cppxdic agreement is *methodological* (sub-0.1 px for the
bulk, divergence where the reference-update strategies differ), while the CPU-vs-
GPU comparison within cppxdic (E3 vs E2) remains exact to 1e-12.

## Follow-up experiments (2026-07-04/05): Points{1} fix + exact_matlab/updates

**Points{1} NaN fix (VERIFIED, jobs 7884334 rerun r3):** root cause was the
`value==0 -> NaN` invalid-point sentinel in formatOutput — cuNCorr's frame-1
self-match returns exactly 0.0 displacement, so the whole reference frame was
declared invalid. Fixed by taking validity from each frame's disp ROI mask
(fillDirectFrame + cam2 interpolation + matching check). GPU rerun now stores
the exact seed grid in Points{1}; all ncorr*.bin tracking outputs bit-identical
to the pre-fix run. Side effect (improvement) on the ncorr path too: pair
results validity is now per-frame-ROI-driven, which SHRANK the MATLAB gap
(pair1 max 54.8->48.6 px, >0.1px 307754->289646, corrcoef mismatches
196267->146254) — the old sentinel corrupted some valid data.

**E4 exact_matlab + updates (job 7885631, `t7_ncorr_cpu32_xm_upd_r1`, 3807 s):**
`use_exact_matlab=true`, `no_update=false` (new config key; KEEP_MOST_POINTS
preset), `cutoff_corrcoef=0.5` — all VERIFIED engaged in run.log
(`exact_matlab_*` on all 4 tracking calls). Outcome: **indistinguishable from the plain path** (identical MATLAB-comparison
profile down to per-element diff counts and max-location) because (a) seed correlations stay at 0.02-0.05 so
the 0.5 update threshold NEVER fires, and (b) with a single segment spanning
all frames, exact_matlab (chain composition at segment boundaries) is
mathematically identical to matlab_*. **The remaining MATLAB gap is therefore
attributable to MATLAB's FIXED-STEP reference updates (step_ref_change=10,
recorded per-frame in its dispinfo) — a mechanism CppNCorr does not implement**
(stepanalysis params are metadata-only). Closing it would mean implementing
fixed-step segmentation in matlab_compute_seed_segment.

Config-placement trap (cost one run): `use_exact_matlab`/`no_update`/
`cutoff_corrcoef` are parsed ONLY from ncorr_params.txt
(Config::loadFromNcorrParamsFile), unprefixed — appending `ncorr_*` keys to
dic_params.txt is silently ignored. Also: the shipped ncorr_params.txt says
`dic_config = KEEP_MOST_POINTS` but that key was never wired to the tracking
call site — the new explicit `no_update` key (default true) preserves the
long-standing behavior instead of honoring it retroactively.

## E6: fixed-step reference updates (2026-07-06, job 7915564, `t7_ncorr_cpu32_xm_fs10_r1`)

Implemented MATLAB's step analysis in CppNCorr behind an option:
`DIC_analysis_parallel_input.fixed_step_ref` (default 0 = off) caps every seed
segment at N frames in `matlab_compute_seed_segment`; the existing segment loop
does the seed propagation and the exact_matlab_* path chains segments back to
the global reference (`exact_add_with_rois` = MATLAB `ncorr_alg_addanalysis`).
cppxdic key: `fixed_step_ref` in ncorr_params.txt; harness axis `FIXEDSTEP=N`
(suffix `_fs<N>`). Verified engaged: "Fixed-step reference updates: every 10
frames", 60 Parallel-dispatch segments = 150 frames / 10 x 4 cams,
exact_matlab_* on all tracking calls. Wall 4612 s (vs 3807 s single-segment —
frame parallelism is capped at 10 within a segment).

MATLAB's own ncorr1.mat dispinfo.stepanalysis reads `enabled=1, type='seed',
auto=1, step=10` — E6 reproduces that structure exactly.

**Result: structurally faithful, numerically WORSE at the 0.1 px level.**
vs E4 (single fixed ref): pair1 points >0.1 px 289,646 -> 505,763 (max 48.6 ->
58.0 px); pair2 165,096 -> 385,820. Corrcoef mismatches slightly IMPROVED
(146k -> 131k, 72k -> 65k) — consistent with segment-local corr values now
aligning — but coordinate chains accumulate port-vs-MATLAB differences at every
one of the 15 boundaries (seed re-optimization + displacement composition each
differ slightly), compounding into more sub-pixel drift than tracking directly
against frame 1. NaN-pattern mismatches also rose (valid-set erosion at
boundaries differs).

**Practical conclusion:** the sub-0.1 px bulk agreement with MATLAB is as good
as it gets without bit-porting MATLAB's per-boundary seed placement and
composition arithmetic; direct fixed-reference tracking (the production
default) is both simpler and CLOSER to the MATLAB reference at the bulk level
than mimicking its stepping. Fixed-step remains available via
`fixed_step_ref` for data where a fixed reference genuinely decorrelates
(large deformations).

## Border-interpolation diagnosis (2026-07-07): the "48 px / 20 px padding" issue

User recalled a historical 48px-class error tied to the 20px interpolation
border. Code archaeology + spatial analysis confirmed the story:

- **The bug**: biquintic B-spline coefficients (`form_bcoef`) were computed by
  FFT circular deconvolution; the circular wrap creates a synthetic
  discontinuity at array edges whose ringing biases values near borders
  (~+29% spectral bias). The DIC interpolation window pads arrays with a
  20-px `border_interp`/`bcoef_border` halo, which mitigates but does not
  remove the bias.
- **The fix**: CppNCorr commit `4758ad2` replaced the FFT path with the
  Unser-Aldroubi-Eden recursive filter (mirror BCs, exact round-trip). It is
  ACTIVE in all current builds and applies to BOTH quintic variants (plain +
  precompute). Bicubic never needed a prefilter, hence "solved for bicubic"
  memories: the workaround era hardcoded CUBIC_KEYS at the perspective-change
  and ROI-warp call sites.
- **MATLAB still has the FFT version** (verified in
  Tools/MultiDIC/.../ncorr_class_img.m form_bcoef: `ifft(fft(...)./kernel)`).
  So near-boundary MATLAB-vs-cppxdic disagreement is expected and OURS is the
  more correct side there.
- **Spatial quantification** (MATLAB vs E4, `spatial_diagnosis.txt`):
  boundary points are 6% of the grid but 23%/45% (pair1/pair2) of rows with
  >1px disagreement; among near-identical rows only 1% are boundary. Excluding
  a 2-cell boundary band drops pair2's max from 9.97 to 3.86 px. Pair1's
  48.6px worst point is INTERIOR though — that residual belongs to the
  fixed-step reseeding methodology difference (see E6 section), not the border.
- **Action taken**: wired the previously dangling `perspective_interp`
  ncorr-params tag (parsed since Mar 2026, consumed by nothing) — `true`
  selects biquintic for the Eulerian perspective change (MATLAB behavior, safe
  post-fix); default `false` keeps the bicubic workaround, preserving all
  existing outputs. The ROI-warp site (`updateMaskAndSeedFromOutput`,
  CUBIC_KEYS + SKIP_INVALID) was left unchanged deliberately.

## Full pipeline (Steps D+E+F) — FIXED AND VERIFIED (2026-07-07)

The historical "segfault at Step E/F" was FOUR stacked bugs, each hidden by the
previous. All fixed; fresh end-to-end runs (`--stages all`, t7, jobs
7974220/7974222) complete with rc=0 on BOTH engines:
**GPU 32 min, ncorr-CPU 75 min end-to-end**, identical stitched meshes
(6,726 points / 12,071 faces), 1.7 GB DIC3DPPresults from Step F.

1. **Step-E save segfault** (`mat_writer.cpp`): AllPairsResults/DIC2Dinfo/ROImask
   were embedded into the combined struct by WRITING them to the output file
   and Mat_VarRead-ing them back through the write handle — the returned
   matvars carry live HDF5-internal state and crash Mat_VarSetStructFieldByName
   (gdb-confirmed). Replaced with pure in-memory builders
   (`buildAllPairsResultsVar`/`buildDIC2DinfoVar`); also removes the duplicate
   top-level variables (~half the file size).
2. **Checkpoint poisoning** (`dic_analysis.cpp`): a crash mid-save left a
   truncated DIC3Dcombined that later runs accepted as a valid checkpoint
   (existence-only test) and then failed on. Saves now write `.partial` and
   rename on success.
3. **Step-F "0 faces" abort** (`mat_reader.cpp`): v7.3 reads defer nested data;
   `ensure_loaded` had been patched in field-by-field and `Faces` (+5 fields)
   never got one. Replaced with one recursive `deep_load` of the struct tree.
4. **cuncorr matching produced all-zero fields** (`step_d_workflow.cpp`):
   inter-camera matching has ~276 px disparities; cuNCorr's local seed search
   (15 px) cannot bridge them (ncorr uses global seed optimization), so
   matching silently failed -> all-NaN cam-def Points -> all-NaN 3D -> the
   stitcher's NaN filter removed every face. Matching calls (<= 2 current
   images) now stay on ncorr; cuNCorr keeps the 150-frame tracking.
   Post-fix, GPU-run cam-def Points are healthy (4% NaN at ROI edges) and
   match the ncorr run's valid sets.

NOTE: all pre-2026-07-07 cuncorr runs have valid 2D tracking but garbage
cam-def/3D data (bug 4). The bit-comparison results (Step-D tracking) are
unaffected.

## Build fixes needed (committed to the working tree)
- `CMakeLists.txt`: `CUDA_SEPARABLE_COMPILATION OFF` on the cuncorr target
  (non-CUDA executables can't device-link an rdc static lib; CMake 3.22 can't
  resolve device symbols on static libs; kernels are TU-local anyway).
- `src/generate_ncorr_bin.cpp`: explicit `H5Rdereference2` behind a version
  guard (Ubuntu 22.04 hdf5 1.10 maps the macro to the 3-arg v1.8 signature).
- `build_sif.sbatch`: APPTAINER_TMPDIR on globalscratch (node /tmp too small
  to squash the nvidia/cuda rootfs).
