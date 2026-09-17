# CPPxDIC — cuNCorr GPU integration + CPU/GPU/MATLAB validation (READ FIRST)

Working notes for the `feat/cuncorr-integration` branch family. Everything here
was learned the hard way in July 2026; trust it before re-deriving.

## Branch structure (stacked PRs on xdic-dev/CPPxDIC)

1. `feat/cuncorr-integration` — cuNCorr engine + GPU image + cpugpu study harness.
2. `feat/fixed-step-ref` (base: 1) — `fixed_step_ref` + `no_update` ncorr-params
   options; bumps `Tools/CppNCorr` to its `feat/fixed-step-ref` branch (01b6e81).
3. `fix/step-ef-pipeline` (base: 2) — Step-E/F segfault chain fixes +
   `perspective_interp` wiring. **Working trees live on this tip = union of all.**

Merge order: 2→1 first, then 3→2. CppNCorr PR: `feat/fixed-step-ref` → main.

## Clusters: TWO separate machines, similar paths (TRAP)

- **Lemaitre4** (`lm4-*`): interactive/login work. **Manneback** (`mbackf*`,
  `ssh manneback` from Lemaitre works passwordless): where ALL cpugpu jobs run
  (GPU partition: A100/L40s/A10/V100/RTX; `keira` = 256-core Milan CPU nodes;
  QOS `normal,preemptible`, gpu limit ~2 jobs/user).
- `/home/ucl/inma/jaoga` is a SYMLINK to `/home/users/j/a/jaoga` on BOTH
  clusters, but the home filesystems are PER-CLUSTER — a file written on one is
  NOT on the other. Sync with
  `rsync -a --exclude='*.sif' --exclude='build/' . manneback:/home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC/`.
  The identical absolute paths mean sbatch scripts work unchanged on both.
- `/globalscratch` is also per-cluster and **PURGEABLE** (a purge destroyed all
  Lemaitre results once; only logs/meta under the repo on home survived —
  that's why the harness writes run.log/meta.txt to the repo results dir).
- **sacct TRAP**: Manneback recycles SLURM job IDs — `sacct -j <id>` without
  `-S <date>` can return a 2018 job with the same ID (false COMPLETED/FAILED).
  Always add `-S <today>`.

## Manneback data layout (S09)

- Videos+protocol: `/globalscratch/ucl/inma/jaoga/data/S09/speckles/coating/{vid,protocol}`
- REF masks/seeds + calib: `/globalscratch/ucl/inma/jaoga/data/analysis/S09/{coating,calib/2}`
- The pipeline resolves videos at `<data_path>/rawdata/<subj>/speckles/<mat>/vid`;
  satisfied by symlink `cpugpu/data_root/rawdata -> ../data` (env.sh makes it).
- MATLAB reference results (Nov-2025 run, trial 007):
  `/globalscratch/ucl/inma/jaoga/cpugpu/matlab_ref/loading/` (from loading.zip;
  `unzip` needs `UNZIP_DISABLE_ZIPBOMB_DETECTION=TRUE` or use bsdtar).
- Frame window is PROTOCOL-DERIVED per trial (idx_frame_start/end in config are
  overridden when protocol .mat files exist). S09 trials: 5(REF),7,12,25.

## The cpugpu harness (`deploy/cluster/cpugpu/`)

- `env.sh` — single place for all paths. Study root
  `/globalscratch/ucl/inma/jaoga/cpugpu/` (SIF + runs/); results+logs under the
  repo on home (purge-safe).
- `build_sif.sbatch` — builds `cppxdic_gpu.sif` from `deploy/cluster/cppxdic_gpu.def`
  (nvidia/cuda 12.4.1, sm 70-89, hdf5-tools, gdb, RelWithDebInfo `cppxdic_dbg`).
  ONE image serves CPU and GPU runs (cuNCorr falls back to CPU without `--nv`)
  so binaries are identical for bit-comparisons. APPTAINER_TMPDIR must be on
  globalscratch (node /tmp too small to squash the CUDA rootfs).
- `run_one.sbatch` — one experiment in an isolated dic_path
  (`runs/<name>`, seeded with REF/calib symlinks; the checkpoint system keys on
  dic_path only, so shared dirs false-skip). Axes (env): TRIAL, ENGINE
  (ncorr|cuncorr), USE_GPU, THREADS, EXACT_MATLAB, UPDATES, FIXEDSTEP, STAGES
  (default `d`; `all` = full pipeline), REP. Every run APPENDS `dic_engine`
  explicitly — **the compiled-in default is cuncorr**.
- **Config-file placement TRAP**: `use_exact_matlab`, `no_update`,
  `cutoff_corrcoef`, `fixed_step_ref`, `perspective_interp` are parsed ONLY
  from **ncorr_params.txt** (unprefixed keys, `Config::loadFromNcorrParamsFile`);
  appending `ncorr_*` keys to dic_params.txt is silently ignored.
- ncorr threading needs BOTH `parallel_processing=true` AND
  `step_d_total_threads=N` (frame-level OpenMP; OMP_NUM_THREADS alone does NOT
  drive it).
- `compare_bits.sh` — bit-level: h5diff on .mat (**never md5** — v7.3 userblock
  embeds timestamps), cmp on .bin.
- `compare_dic.cpp` — matio comparator for MATLAB↔cppxdic files (MATLAB = v5 +
  struct-array layouts, unreadable/misaligned for h5diff). Modes: two-file walk,
  POINTS (point-matched trajectories), DUMP/LIST probes. Build inside the SIF:
  `g++ -O2 -o compare_dic compare_dic.cpp -lmatio`. NOTE: any matio reader of
  v7.3 files MUST deep-load recursively (Mat_VarReadDataAll on every nested
  struct field/cell) — deferred data otherwise reads as silently empty.
- `debug_stepe.sbatch` — rerun selected `--stages` on an existing run dir under
  gdb (`cppxdic_dbg`), for backtraces. RUN=<name> STAGES=<spec> LOGLEVEL=debug.

## Config keys that used to be silently ignored (fixed Sept 2026, fix/config-driven-pairs-mng-data)

- `camera_pairs` is now the ONLY source of the stereopair -> cameras table
  (`Utils::setCameraPairs`, registered in main.cpp / DicAnalysis ctor). Before,
  every call site used the hard-coded {1,2 ; 4,3}: pair >= 3 mapped to cams
  (1,2) -> "could not seed any current image" on multi-pair rigs.
- `im_saturation_mode` (default true) — set false for videos already filtered
  upstream (MNG rigs): satur.m clipping flattens the speckle and ncorr diverges
  ("hessian failed" with huge p1/p2). `limit_grayscale = 0` = auto (subject
  NUMBER rule: S01..S07 -> 70, else 100; ids without digits, e.g. "Artem", -> 70).
- `force_frame_window` (default false) — idx_frame_start/end/jump authoritative;
  otherwise ANY `*.mat` in the protocol dir silently sets the per-phase window.
  A protocol .mat is optional when `ref_trial_id` is set (MNG `.tsv` protocols
  are not parsed).
- ncorr_params `cutoff_max_corrcoef` / `cutoff_max_diffnorm` /
  `seeds_are_optimized` now really reach the engine (engine defaults 0.5 / 0.1
  applied before). Inter-view MATCHING calls (<= 2 current images) use
  `matching_cutoff_max_corrcoef` (default 1.0): the MNG pair-1 matching seed
  converges at corrcoef 0.51 and the 0.5 tracking gate rejected it — that was
  the whole July "mirrored mode cannot match" story.
- Video lookup: strict `<subj>_<mat>_speckles_<trial>_*_cam_<id>.mp4` first,
  then a relaxed `*_<trial>_*_cam_<id>.mp4` fallback (logged as WARN).
- `video_quality = high` no longer crashes (stoi); `dic_engine` MUST be `ncorr`
  in CPU-only images (compiled default cuncorr = single-thread CPU fallback).
- External user's 8-view dataset ("Artem", 4 pairs 1,2;3,4;5,6;7,8, per-view
  968x1216 pre-filtered videos) lives on LEMAITRE:
  `/globalscratch/ucl/inma/jaoga/louis/`; rerun harness in `../louis_run/`.
- Mirrored (MNG) mode work is a SEPARATE worktree/branch:
  `../CPPxDIC-mirrored` = `feat/mirrored-mode-mng` (stacked on the fix branch);
  Manneback copy synced WITHOUT .git (`.build_commit` carries the SHA), runs in
  `/globalscratch/ucl/inma/jaoga/mirrored_run/`.

## Key findings (details in deploy/cluster/cpugpu/results/REPORT.md)

- **Perf (Step D, 150-frame trial)**: GPU (1×A100) ~17 min ≈ 3.7-4× faster than
  ncorr CPU-32-thread (~65-77 min) at ~15× less RAM (3.7 vs ~55 GB). Full
  pipeline (D+E+F): GPU 32 min, CPU 75 min. cuNCorr CPU backend is
  single-threaded (SessionConfig.num_threads is never consumed).
- **Bit-parity**: GPU deterministic across reps; cuncorr CPU-vs-GPU differences
  bounded by 1e-12 (ULP noise; cams 2/3 bit-exact). ncorr↔cuncorr NOT
  bit-comparable (different valid-point sets, ~0.2% at ROI edges).
- **MATLAB agreement**: sub-0.1 px for the bulk; the boundary band of
  disagreement is MATLAB's FFT-bcoef border bias (still present in MATLAB's
  form_bcoef; fixed on our side by CppNCorr 4758ad2 recursive filter) — cppxdic
  is the more correct side there. The interior tail traces to MATLAB's
  fixed-step reseeding (stepanalysis step=10): reproducing it via
  `fixed_step_ref=10` matches the structure exactly but agrees WORSE at 0.1 px
  (boundary compositions compound) → production default stays fixed-reference.
- **cuNCorr CANNOT do inter-camera matching** (~276 px disparity ≫ its 15 px
  local seed search; it silently returns all-zero fields with an empty ROI).
  Matching calls (≤2 current images) are routed to ncorr in the dispatch —
  do not "optimize" that away.
- **Step-E/F bug chain (all fixed 2026-07-07)**: (1) mat_writer write-then-
  read-back matvar embedding segfaulted `Mat_VarSetStructFieldByName`;
  (2) crash-truncated DIC3Dcombined poisoned the existence-only checkpoint —
  saves now go `.partial`→rename; if a run crashed mid-E, DELETE the leftover
  DIC3Dcombined before rerunning; (3) v7.3 deferred reads left Faces empty
  ("0 faces" Step-F abort) — mat_reader now deep-loads.
- Historical Lemaitre profiling study (threading, seed-opt, import fix) lives
  in the `ja/profiling` branch CLAUDE.md; that OMP_NUM_THREADS-only sweeps
  measure nothing still applies.

## Branch structure — SEPTEMBER 2026 UPDATE (read this, the section above is July)

Everything in the July stack is merged into `main` (PR #41). Current work:

1. `fix/config-driven-pairs-mng-data` (12 commits on `main`, pushed) — the config keys
   that were parsed but ignored, the ncorr seed gates, `step_e_stitch_mode`,
   `step_e_ray_bvh`, and two reports under `deploy/cluster/{artem,mirrored}/REPORT.md`.
   Keeps `Tools/CppNCorr` pinned to `feat/fixed-step-ref` (01b6e81) because `main` uses
   `fixed_step_ref`, which CppNCorr `main` does not have.
2. `feat/mirrored-mode-mng` (3 commits, stacked on 1, pushed) — worktree at
   `../CPPxDIC-mirrored`.

`Tools/MultiDIC` shows as modified in the worktree (recorded 0466ade vs checked-out
f0e4877). Pre-existing drift from c7c574c — do NOT commit it.

## Cluster timing trap (measured 2026-09-14)

Lemaitre is **faster** than Manneback on Step D (multi-threaded) and **5-15x slower** on
Step F (single-threaded, memory-bound); Step E geometric stitching behaves like Step F.
Never compare Step E/F timings across the two clusters, and run them on Manneback.
This — not the stitching algorithm — is what made a 9-minute stitch take 5 hours there.

## Things that look like bugs and are not (or are)

- `camera_pairs` ordering is part of the calibration: `6,5` and `5,6` are different runs
  (it decides which camera is the reference, hence which REF mask/seed applies).
- Geometric stitching on the 2-pair S09 inputs silently yields **0 faces** and reports
  success (the July 7a0e3ce image does the same). Undiagnosed.
- The 3.7-4.2x cuNCorr speedup from the S09 study does NOT reproduce on the Artem rig
  (1.15x). Open — see the memory note `gpu-speedup-regression`.

## Routine operations

```bash
# rebuild image after code changes (from repo root ON MANNEBACK):
sbatch deploy/cluster/cpugpu/build_sif.sbatch                # ~25-40 min warm
# full grid (Step D comparisons):
deploy/cluster/cpugpu/submit_all.sh                          # TRIALS/ONLY env to narrow
# one full-pipeline run:
TRIAL=7 ENGINE=cuncorr USE_GPU=1 THREADS=8 STAGES=all REP=9 \
  sbatch -p gpu --gres=gpu:1 --constraint='TeslaA100|TeslaA100_80' \
  -c 8 --mem=64G -t 12:00:00 deploy/cluster/cpugpu/run_one.sbatch
# bit compare / MATLAB compare after runs:
deploy/cluster/cpugpu/compare_bits.sh --all
sbatch deploy/cluster/cpugpu/compare_matlab.sbatch
```
Never reuse a run NAME unless you intend to wipe it (run_one rm -rf's its own
dic_path). Meta/logs land in `deploy/cluster/cpugpu/results/<name>/` on home.
