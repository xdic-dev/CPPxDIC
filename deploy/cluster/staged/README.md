# Staged (decomposed) cluster execution

Run the pipeline as **three independent SLURM array stages** instead of one
monolithic job, so the embarrassingly-parallel work fans out across the cluster:

| Stage | Granularity (one array task per…) | `cppxdic` invocation | Produces |
|-------|-----------------------------------|----------------------|----------|
| 1. matching | `(trial, pair)` | `--trial T --pair P --stages match` | `MATCHING2*_pair{P}.bin`, `ncorr{c1}{c2}.bin`, `dic_info_*_pair{P}.mat` (ROI/seed inside) |
| 2. tracking | `(trial, pair, camera)` | `--trial T --pair P --cam C --stages track` | `ncorr{C}.bin` |
| 3. format+E+F | `(trial)` | `--trial T --stages format,e,f` | `myDIC2DpairResults_*`, `DIC3Dcombined_*`, `DIC3DPPresults_*` |

This is possible because every sub-step **checkpoints to disk** and downstream
sub-steps **load those checkpoints** — so each stage's process only does its own
work and reads the rest from disk. See the `--stages` / `--pair` / `--cam` flags
in `cppxdic --help`.

## Quick start

```bash
cd <workdir with dic_params.txt + ./build/cppxdic (or set SINGULARITY_IMAGE)>

# One command — submits all three stages with SLURM dependencies between them:
SUBJECT=S09 REFTRIAL=5 TRIALS="7 12 25" PAIRS="1 2" \
  ./deploy/cluster/staged/submit_staged.sh
```

It writes three manifests (`manifest_{matching,tracking,ef}.txt`), then submits
three array jobs where stage *N+1* starts only after stage *N* fully succeeds
(`--dependency=afterok`). Within a stage all tasks run in parallel (capped by
`CONC`, default 40).

## Manual submission (one stage at a time)

```bash
# generate manifests
TRIALS="7 12 25" PAIRS="1 2" ./deploy/cluster/staged/gen_stage_manifests.sh

# stage 1
sbatch --array=1-$(wc -l < manifest_matching.txt) \
       --export=ALL,SUBJECT=S09,REFTRIAL=5,MANIFEST=manifest_matching.txt \
       deploy/cluster/staged/run_stage_matching.sbatch

# stage 2 (after stage 1)
sbatch --dependency=afterok:<jobid1> --array=1-$(wc -l < manifest_tracking.txt) \
       --export=ALL,SUBJECT=S09,REFTRIAL=5,MANIFEST=manifest_tracking.txt \
       deploy/cluster/staged/run_stage_tracking.sbatch

# stage 3 (after stage 2)
sbatch --dependency=afterok:<jobid2> --array=1-$(wc -l < manifest_ef.txt) \
       --export=ALL,SUBJECT=S09,REFTRIAL=5,MANIFEST=manifest_ef.txt \
       deploy/cluster/staged/run_stage_ef.sbatch
```

## Pair → camera mapping

`gen_stage_manifests.sh` must enumerate the cameras of each pair, matching
`Utils::getCamerasForPair()` in the binary. The default S09 2-pair layout is:

```
pair 1 -> cameras 1 2     (myDIC2DpairResults_C_1_C_2)
pair 2 -> cameras 4 3     (myDIC2DpairResults_C_4_C_3)
```

Override with `CAMS_FOR_PAIR_1` / `CAMS_FOR_PAIR_2` if your rig differs.

## Binary vs Singularity

Both paths are supported by every script:
- **Binary**: default `CPPXDIC_BIN=./build/cppxdic`, run from a `WORKDIR` that
  contains `dic_params.txt` / `ncorr_params.txt` / `visualization_params.txt`
  with `base_path` / `dic_path` set to your data / output trees.
- **Singularity**: set `SINGULARITY_IMAGE=/path/cppxdic.sif` (built per the
  parent [`../README.md`](../README.md)); the same config convention applies via
  the bind mounts described there.

## Resource sizing (defaults in the `#SBATCH` headers; override per submission)

From local profiling (S09/coating). All are overridable with
`sbatch --cpus-per-task=… --mem=… --time=…`.

| Stage | cpus | mem | time | rationale |
|-------|-----:|----:|-----:|-----------|
| matching | 4 | 24G | 00:45 | imports the full frame stack (~10 GB for 141 frames) + matching (~90 s) |
| tracking | 4 | 24G | 03:00 | frame stack + ~1.2 GB/thread; tracking is content-driven (minutes to >1 h) |
| format+E+F | 2 | 16G | 00:30 | loads binaries only; E ~ seconds, F ~ tens of seconds |

**Threads:** the tracking stage pins `OMP_NUM_THREADS` to `--cpus-per-task`. To
push thread counts, raise `--cpus-per-task` **and** `--mem` together and set
`step_d_total_threads` in the config to match. Keep
`OMP_NUM_THREADS × step_d_total_threads ≤ cpus-per-task` (nesting oversubscription
slows tracking — see the local study). For a deeper thread/memory scaling study
see `HPC_SLURM_GUIDE.md` in the trial-7 study archive.

## Notes

- Stages are **resumable**: a sub-step whose checkpoint already exists is skipped,
  so re-submitting only fills the gaps.
- `--dependency=afterok` waits for the *entire* previous stage; this is the simple
  correct ordering (a tracking task strictly only needs its own pair's matching,
  but whole-stage dependency keeps submission trivial).
- The tracking stage re-imports + filters frames (those are not cached between
  stages). That preprocessing (~3 min for 141 frames) is the only duplicated work.
