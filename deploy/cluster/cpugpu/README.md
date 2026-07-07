# CPU vs GPU (ncorr vs cuNCorr) comparison study — MANNEBACK

Branch: `feat/cuncorr-integration`. Subject S09/coating, trials 7, 12, 25,
Step D only (`--stages d`). Everything runs from ONE image (`cppxdic_gpu.sif`,
CUDA build with runtime CPU fallback) so all experiments use an identical binary.

| Exp | dic_engine | Hardware | Threads | Why |
|-----|-----------|----------|---------|-----|
| E1  | ncorr     | keira CPU node | 32 (frame-parallel) | production CPU baseline |
| E2  | cuncorr   | gpu partition, A100, `--nv` | — (CUDA) | the GPU run (r1 + r2 for determinism) |
| E3  | cuncorr   | keira CPU node (no `--nv`) | 1 (cuNCorr CPU path is single-threaded) | attributes E1↔E2 gaps: engine vs CUDA |

Expected outcome (from the integration design): E2 ↔ E3 should be bit-identical
(shared cores, `cuda_parity_test`); E1 ↔ E2 is a *different algorithm*
(ncorr updates reference every 10 frames; cuNCorr sequence mode = fixed
reference + warm start) so exact bit-identity is NOT expected there — h5diff
quantifies how close they are.

## Order of operations
```bash
# on manneback, repo root:
sbatch deploy/cluster/cpugpu/build_sif.sbatch        # ~30-60 min
deploy/cluster/cpugpu/submit_all.sh                  # 12 jobs
deploy/cluster/cpugpu/compare_bits.sh --all          # after all runs finish
```

## Layout
- image + big DIC outputs: `/globalscratch/ucl/inma/jaoga/cpugpu/{cppxdic_gpu.sif,runs/}`
- run.log + meta.txt + sbatch logs: `deploy/cluster/cpugpu/{results,logs}/` **on home**
  (globalscratch was purged 2026-07 and only home copies survived — keep it this way)
- input data: `/globalscratch/ucl/inma/jaoga/data/S09` (videos+protocol),
  `/globalscratch/ucl/inma/jaoga/data/analysis/S09` (REF_*.mat, calib/2)
- `data_root/rawdata -> ../data` staging symlink satisfies the pipeline's
  `<data_path>/rawdata/<subject>/speckles/<material>/vid` resolution.

## Gotchas encoded in the scripts
- `dic_engine` compiled-in default is **cuncorr** — every run appends it explicitly.
- ncorr threading needs BOTH `parallel_processing=true` AND `step_d_total_threads=N`.
- Never md5 .mat v7.3 (userblock timestamp) — `compare_bits.sh` uses h5diff/cmp.
- All grep|sort|head classify pipelines are pipefail-safe (`|| true`).
