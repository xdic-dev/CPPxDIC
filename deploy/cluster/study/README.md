# CPPxDIC profiling study harness (SLURM)

Self-contained scripts to build the instrumented image and run the Step-D
thread/frame scaling study described in `MultiDIC/HPC_SLURM_GUIDE.md`, adapted
to this cluster and the real `ja/profiling` code.

## Files
| file | role |
|---|---|
| `env.sh` | **single source of paths/params** — edit here |
| `build_sif.sbatch` | build `cppxdic_prof.sif` on a compute node (has internet) |
| `smoke_test.sbatch` | 2 forced frames; validates image + force-frames + REF/calib |
| `run_one.sbatch` | one experiment, isolated & checkpoint-safe (the study unit) |
| `status.sh` | **the status hook** — build/queue/per-experiment dashboard |

## Why each experiment is isolated (critical)
The pipeline checkpoints on existing files, and output paths key only on
`dic_path/subject/material/trial/phase` (`Utils::buildOutputPath`) — **not** on
thread count or frame window. Two configs of the same trial sharing a `dic_path`
would make the 2nd run load the 1st's results and skip computation (ruining the
timing). So every experiment gets its **own `dic_path`** under `runs/<name>/`,
seeded with symlinks to the shared `REF_*.mat` (under `<subj>/coating/`) and
`calib` (under `<subj>/calib/`). Parallel jobs then never touch the same path.

## Key path facts (this data set)
- Videos resolve at `data_path/rawdata/S09/speckles/coating/vid` ⇒
  `data_path = .../MultiDIC/example_data`.
- REF + calib are read relative to `dic_path` ⇒ each isolated `dic_path` is seeded.
- `material_id=2` → `coating`. `reftrial=5` (only `REF_*_005_*` exist).
- Force-frames: `XDIC_FORCE_FRAMES=1` makes `idx_frame_start/end/jump`
  authoritative (gated patch in `src/utils.cpp`); otherwise the protocol decides.
- Profiling: `XDIC_PROFILE=1` + `XDIC_PROFILE_OUT=<dir>` → `xprof_*.csv`.

## Run it
```bash
cd <repo root>                      # always submit from here (relative log paths)
BUILD=$(sbatch --parsable deploy/cluster/study/build_sif.sbatch)
SMOKE=$(sbatch --parsable --dependency=afterok:$BUILD deploy/cluster/study/smoke_test.sbatch)
TRIAL=7 FSTART=10 FEND=59 OMP=8 STEPD=1 \
  sbatch --dependency=afterok:$SMOKE --cpus-per-task=8 --mem=24G --time=01:00:00 \
         deploy/cluster/study/run_one.sbatch
./deploy/cluster/study/status.sh           # check progress anytime
```

## Scaling out later (parallel, not sequential)
Submit many `run_one.sbatch` with different env params **at once** (own
`dic_path` each ⇒ safe). Match resources to the row:
`--cpus-per-task = OMP*STEPD`; `--mem ≈ ceil(5 + 0.05*frames + 1.5*threads)*1.25`
(floor 16G); short `--time` backfills fastest on this busy cluster. Keep
`OMP*STEPD ≤ cpus-per-task` (run_one enforces this). The cluster has 256-core /
766 GB nodes, so the local 8-core oversubscription ceiling does not apply.
