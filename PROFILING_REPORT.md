# CPPxDIC Preprocessing + Steps D/E/F — Time & Memory Profile

**Date:** 2026-06-18
**Subject/dataset:** `S09` / `coating`, reference trial **005**, phase `loading`, `num_pair = 2`
**Workload:** one trial through the full pipeline — preprocessing → Step D (2D DIC) → Step E (3D reconstruction) → Step F (deformation/strain), default compiled parameters (frames 10–150, jump 1 ⇒ 141 frames/cam; `im_filter_mode=on`; `OMP_NUM_THREADS=4`).
**Data:** videos from `example_data/rawdata/S09/speckles/coating/vid/`, calibration from `analysis/S09/calib/2/`, outputs to `analysis/S09/coating/005/loading/`.

---

## TL;DR

| | |
|---|---|
| **Total wall time** | **3663.5 s ≈ 61.1 min** |
| **Peak RSS (max resident)** | **9.99 GB** (`/usr/bin/time -l`) |
| **Peak memory footprint** (incl. compressed) | 13.82 GB |
| **CPU utilisation** | user 6874 s / real 3664 s ⇒ ~1.9 cores avg (OMP=4, tracking is largely serial per frame) |
| **Dominant cost** | **Step D 2D-DIC tracking = ~90 % of time and the entire 9.5 GB memory peak** |
| **Preprocessing (video load + filtering + ROI/seed)** | **240.7 s ≈ 4.0 min = 6.6 %** of wall time |
| **Step E / Step F** | trivial: 3.2 s (0.09 %) / 18.9 s (0.52 %) |

> The pipeline is **overwhelmingly bound by the NCorr 2D correlation/tracking inside Step D.** Preprocessing and the 3D/strain stages (E, F) are comparatively cheap in both time and memory.

---

## Environment

- **Machine:** Apple Silicon (arm64), 8 perf/effic. cores, 18 GB RAM, macOS (Darwin 25.4).
- **Binary:** `build/cppxdic` (native arm64, `-O2`, Release-equivalent), built from this tree with the scratch profiler (`include/profiling.h`) compiled in.
- **Measurement:**
  - Per-phase wall time + peak RSS via an env-gated in-process profiler (`XDIC_PROFILE=1`) that wraps each phase with `XPROF_SCOPE(...)` and samples RSS every 50 ms on a background thread.
  - Whole-process peak via `/usr/bin/time -l` (`maximum resident set size`, `peak memory footprint`).
  - RSS read from the Mach task (`mach_task_basic_info.resident_size`); peak-per-phase = max RSS sampled within that phase's enter/exit window.
- **Docker note:** A containerised cross-check (Ubuntu 22.04, same arm64 binary) was **not run**: Docker Desktop's Linux VM is capped at 7.7 GB while this workload peaks at ~9.5 GB, so the container would swap/OOM. Native numbers are authoritative; the same `XPROF` instrumentation works unchanged in the container if the VM RAM is raised to ≥12 GB. A `deploy/docker/Dockerfile.prof` (builds only `cppxdic`) and `tests/run_docker_profile.sh` are provided for that path.

---

## Per-phase breakdown (summed over both stereo pairs)

| Phase | Time (s) | % wall | Peak RSS (MB) | Group |
|---|---:|---:|---:|---|
| `D.preproc.import_video` | 72.9 | 1.99 % | 1025 | preprocessing |
| `D.preproc.saturation` | 0.6 | 0.02 % | 1642 | preprocessing |
| `D.preproc.roi_seed_match` | 95.5 | 2.61 % | 2033 | preprocessing |
| `D.preproc.filtering` | 71.7 | 1.96 % | 3730 | preprocessing |
| `D.matching_cams` | 105.5 | 2.88 % | 3844 | DIC core |
| `D.tracking1` (cam 1) | **2195.1** | **59.9 %** | **9515** | DIC core |
| `D.tracking2` (cam 2) | **1092.9** | **29.8 %** | **9490** | DIC core |
| `D.format_output` | 6.4 | 0.17 % | 772 | save |
| **STEP_D_total** | **3641.3** | **99.4 %** | **9515** | — |
| **STEP_E_total** (3D reconstruction) | 3.2 | 0.09 % | 1171 | — |
| **STEP_F_total** (deformation/strain) | 18.9 | 0.52 % | 3113 | — |
| **OVERALL** | **3663.5** | 100 % | **9515** | — |

### Grouped

| Group | Time (s) | % wall |
|---|---:|---:|
| **Preprocessing** (import + saturation + ROI/seed + filtering) | **240.7** | **6.6 %** |
| **DIC core** (camera matching + tracking 1 + tracking 2) | **3393.5** | **92.6 %** |
| **Step E** (3D reconstruction) | 3.2 | 0.09 % |
| **Step F** (deformation/strain) | 18.9 | 0.52 % |
| Output formatting / save | 6.4 | 0.17 % |

---

## Per-pair detail (the biggest single finding)

The two stereo pairs run **sequentially**, and tracking time is **highly asymmetric**:

| Phase | Pair 1 (C_1↔C_2) | Pair 2 (C_4↔C_3) |
|---|---:|---:|
| import_video | 34.6 s | 38.3 s |
| roi_seed_match | 49.4 s | 46.1 s |
| filtering | 33.2 s | 38.5 s |
| matching_cams | 62.1 s | 43.4 s |
| **tracking1** | **1645.7 s** | **549.4 s** |
| **tracking2** | **575.4 s** | 517.6 s |
| format_output | 3.3 s | 3.0 s |
| **Pair total (Step D)** | **≈ 2403 s** | **≈ 1237 s** |

- **Pair-1 `tracking1` alone (1646 s, 27 min) is 45 % of the entire run.** It is ~3× slower than pair-2's `tracking1` (549 s) and ~3× slower than pair-1's own `tracking2` (575 s), despite pair 1 producing only ~20 % more surface points (3672 vs 3075). This asymmetry — not raw frame count — is the dominant lever and the first thing worth investigating (likely correlation-convergence / bad-subset replacement behaviour or ROI size on cam 1 of pair 1).

---

## Memory profile

- **Peak RSS ≈ 9.5 GB**, reached **during Step D tracking**. Steps E and F never exceed ~1.2 / ~3.1 GB.
- **Timeline shape:** a two-hump sawtooth. Within each pair, RSS climbs through tracking to multi-GB highs (transient spikes to 8–9.5 GB) then collapses back to ~0.4 GB at the pair boundary (buffers freed between pairs). Baseline during preprocessing is ~0.3–2 GB.
- **What drives the peak:** Step D holds all ~141 frames per camera **plus** the NCorr DIC result structures (per-frame displacement/ROI/seed fields, plus the bad-subset replacement working set) resident simultaneously across the frame sequence. This is inherent to the current all-in-memory tracking design, not preprocessing.
- Preprocessing peaks are modest: video import ~1.0 GB, filtering ~3.7 GB (it materialises filtered copies of the full saturated frame stack).

---

## Disk output footprint (per trial)

- `analysis/S09/coating/005/loading/` ⇒ **8.7 GB**
- `analysis/S09/coating/tmp_frames/T5/` (extracted PNG frames) ⇒ **525 MB**
- Note: NCorr results are written **twice** — both `ncorrN.bin` *and* `ncorrN.mat` are emitted for every camera/pair, roughly doubling the on-disk result size. Worth confirming both formats are actually needed.

---

## Bottlenecks & recommendations (in priority order)

1. **Investigate pair-1 `tracking1` (1646 s).** A 3× per-pair tracking asymmetry is the single largest opportunity; if pair 1 tracked at pair-2 speed the whole run would drop by ~18 min (~30 %).
2. **Parallelism headroom.** Avg ~1.9 cores busy with `OMP_NUM_THREADS=4` on an 8-core box ⇒ tracking is largely serial per frame. Raising thread count and/or processing the two pairs concurrently could cut wall time substantially (at higher peak memory — only viable with ≥16 GB headroom).
3. **Memory:** the ~9.5 GB peak is tracking-resident frame+result data. Streaming/windowing frames or releasing per-frame NCorr buffers sooner would lower the peak and make a 8 GB container/VM viable.
4. **Disk:** drop the redundant `.bin`+`.mat` double-write if only one format is consumed downstream (≈ halves the 8.7 GB/trial footprint).
5. **Cheap stages:** Step E (3.2 s) and Step F (18.9 s) need no optimisation attention.

---

## Reproduction

```bash
# Build (native) with the scratch profiler compiled in:
cmake --build build -j

# Run one trial through D→E→F with profiling, pure compiled defaults.
# (NB: the legacy root-level *_params.txt files have a pre-existing stoi parse
#  bug; pointing all four param flags at the all-commented config/default.cfg
#  sidesteps it and uses compiled defaults.)
XDIC_PROFILE=1 XDIC_PROFILE_OUT=/tmp/xprof_native OMP_NUM_THREADS=4 \
  /usr/bin/time -l ./build/cppxdic \
    -C config/default.cfg -d config/default.cfg -n config/default.cfg -v config/default.cfg \
    --subject S09 --reftrial 5 --trial 5

# Outputs: /tmp/xprof_native/xprof_{phases,events,samples}.csv  + the [XPROF] table on stderr.

# Restore the data folder to its clean (checkpoint-free) state so the next run
# re-executes every step:
bash tests/restore_coating_clean.sh
```

### Artifacts added (scratch profiling — env-gated, zero overhead unless `XDIC_PROFILE` is set)
- `include/profiling.h` — header-only time+RSS profiler (Mach on macOS, `/proc` on Linux).
- Instrumentation `XPROF_SCOPE(...)` in `src/dic_analysis.cpp` (D/E/F) and `src/step_d_workflow.cpp` (preprocessing/matching/tracking/format); `dump()` call in `src/main.cpp`.
- `tests/restore_coating_clean.sh` — resets `analysis/S09/coating` to the 4 `REF_*` files.
- `deploy/docker/Dockerfile.prof`, `tests/run_docker_profile.sh`, `.dockerignore` — for the optional containerised run.

### Caveats
- **Checkpointing:** the data folder was reset to its clean state after the run (verified identical to `coating-back`), so reruns are not skipped.
- **Sampler resolution:** during the heaviest tracking the 50 ms RSS sampler is CPU-starved, so the mid-run timeline is sparse; per-phase peaks still come from the in-scope reads and available samples, and the whole-process peak is corroborated by `/usr/bin/time -l`.
- Numbers are for a **single trial, single machine, one run** — treat as representative magnitudes, not benchmarks.
