# CPPxDIC profiling & app-test harness — run guide

Everything here runs on the **CECI `lm4` SLURM cluster** (single `batch` partition, 256 CPU /
766 GB nodes). Compute nodes have internet (so the Apptainer image builds in a job). You may NOT
run compute on the login node — always go through SLURM. `~/devlab/MultiDIC/CPPxDIC` is the repo;
data + outputs live on `/globalscratch/ucl/inma/jaoga/MultiDIC`.

All paths are centralised in **`deploy/cluster/study/env.sh`** — edit there if you relocate things.

---

## 0. One-time: build the instrumented image

```bash
cd ~/devlab/MultiDIC/CPPxDIC
sbatch deploy/cluster/study/build_sif.sbatch      # ~2-3 min on a compute node
```
Produces `deploy/cluster/cppxdic_prof.sif`. The `.def` builds `cppxdic` **plus** the optional apps
(`singledic`, `proxyncorr`, `gen_subject_trial`) via `-DBUILD_*=ON`, and bakes the working tree
(XPROF instrumentation, force-frames patch, the seed-opt + tmp-dir fixes). Verify:
```bash
apptainer exec deploy/cluster/cppxdic_prof.sif /opt/cppxdic/bin/entrypoint.sh --list-apps
```

---

## 1. Threading model (READ FIRST — see ../../CLAUDE.md for the full story)

Step-D parallelism needs BOTH, in the config:
```
parallel_processing  = true     # master switch (shipped default is false!)
step_d_total_threads = N        # the REAL thread count
```
`OMP_NUM_THREADS` does NOT drive it (the frame loop uses `num_threads(min(frames,N))`). Enable the
parallel per-frame **seed optimization** with env `XDIC_PARALLEL_SEEDOPT=1` (bit-identical, ~2x
tracking). Prove parallelism engaged: grep run.log for `Parallel dispatch`.

Findings (S09 coating, 141-frame window): tracking scales to ~32 threads then saturates; end-to-end
Step-D ~1650 s @32 threads after the seed-opt + upstream import fix (PR #35). 64 threads ≈ 0 gain,
2x memory (~1.7 GB/thread → ~110 GB @64).

---

## 2. Thread / frame scaling study

```bash
cd ~/devlab/MultiDIC/CPPxDIC
# edit the axes in gen_manifest.sh (TRIALS, THREADS, REPS, window) if needed, then:
PAR_SEEDOPT=1 bash deploy/cluster/study/submit_grid.sh 4     # %4 concurrent per array
bash deploy/cluster/study/status.sh                          # dashboard (--watch / --csv)
bash deploy/cluster/study/status.sh --csv > summary.csv      # aggregate
# recover meta.txt if a run's post-processing died (idempotent):
bash deploy/cluster/study/regen_meta.sh
# cancel everything:  scancel $(awk '{print $1}' deploy/cluster/study/.grid_jobids)
```
Each experiment gets an **isolated `dic_path`** (`_study/runs/<name>`) seeded with REF/calib
symlinks — required, or the checkpoint system false-skips (`see cppxdic-checkpoint-isolation`).
Knobs: `PAR_SEEDOPT=1` is the live optimization; window 10-150 (141 frames) is the standard.

---

## 3. Optional-app tests (`deploy/cluster/study/apps/`)

All isolated, 32 threads, S09, 141-frame window where feasible. Helper `setup_isolated.sh <name>
<threads> <fstart> <fend> <jump>` makes a seeded dic_path + config.

```bash
cd ~/devlab/MultiDIC/CPPxDIC
SD=deploy/cluster/study/apps

# (a) trial-batch: cppxdic --trials, one array task per trial
ROOT=$(bash $SD/setup_isolated.sh trial_batch 32 10 150 1 | tail -1)
ROOT="$ROOT" TRIALS_CSV="7,12,25" sbatch --array=1-3 $SD/trial_batch.sbatch
#   GOTCHA: apptainer --cleanenv strips SLURM_ARRAY_TASK_ID — it is passed back in explicitly.

# (b) staged: cam-granularity decomposition, 3 dependent arrays
TRIALS="7 12 25" PAIRS="1 2" bash $SD/staged_run.sh
#   match (trial,pair) -> track (trial,pair,cam) @32 -> ef (trial). Safe concurrency thanks to the
#   per-output tmp dir fix (tmp_ncorr_<stem>); see ../../CLAUDE.md.

# (c) singledic: single-camera 2D DIC (no stereo/3D)
TRIAL=7 CAM=cam_1 START=10 END=39 sbatch $SD/singledic.sbatch     # ~270 s/frame -> use small window
#   adapts S09 data into <data>/vid/S09/coating/<trial>.mp4 automatically.

# (d) proxyncorr: pass-through ncorr over an image folder
TRIAL=7 PAIR=1 FSTART=10 FEND=150 sbatch $SD/proxyncorr.sbatch
#   self-extracts S09 frames + renders the REF mask to roi.png (REF files are MATLAB-5, unreadable
#   off-pipeline). Uses --in-memory (the --seeds parallel path segfaults — proxyncorr bug).
```
Outputs land under `_study/runs/apps/<name>/`. Each app's `.sbatch` prints rc + key evidence.

---

## 4. Gotchas / known issues

- **Checkpoint isolation**: one `dic_path` per concurrent unit; the checkpoint keys only on
  `dic_path/subject/material/trial/phase`, so sharing makes parallel jobs false-skip.
- **`set -euo pipefail`**: every `grep|...|head` extraction in the harness ends `|| true` (a no-match
  grep returns 1 and `head` SIGPIPEs `sort`). Don't remove those.
- **`.mat` bit-comparison**: use `h5diff` on the datasets, never `md5` (v7.3 embeds a timestamp +
  the dic_path). REF_MASK/REF_SEED are MATLAB-5 (no h5/scipy/matio/ffmpeg here).
- **singledic** ~100x slower than the frame-parallel path (classic spatial RGDIC); 141 frames ≈ 11 h.
- **proxyncorr** `--mode parallel --seeds` segfaults; `--in-memory` works.
- Size walltime generously — a SLURM timeout SIGKILL can't be caught and loses Step-D data (the
  XPROF emergency handler only catches SIGSEGV/ABRT/etc., not SIGKILL).
