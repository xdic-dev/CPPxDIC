# singledic — single-camera 2D DIC (no stereo / no 3D)

`singledic` runs 2D Digital Image Correlation on a **single** camera's image
sequence and exports in-plane displacement (`u`, `v`) and per-point correlation
fields. Unlike the full `xdic` camerapairs pipeline it performs **no stereo
pairing and no 3D surface reconstruction** — it is "step D" for one camera with
a single reference frame.

> **Status:** Implemented (engine + driver). Final CMake wiring is applied at
> integration (see the wiring snippet below).

## MATLAB behavioural reference

This reproduces Viktoriia's single-camera **"sliding analysis"** step-D
workflow. Reference files (read-only) under
`Tools/MultiDIC/lib_script/sliding_analysis/viktoriia_script_analysis/`:

| MATLAB file | Role | C++ counterpart |
|---|---|---|
| `process_single_trial_ncorr.m` | Single-trial driver: read video → saturate → (filter) → ROI/seed → (matching) → **one** tracking pass | `singledic::SingleDicWorkflow::run()` |
| `draw_ref_roi_single_trial.m` | ROI on the reference frame | `loadOrCreateRoi()` (load `REF_MASK_*.mat`, else full-frame ROI) |
| `draw_ref_seed_single.m` | Seed point on the reference | `loadOrCreateSeed()` (load `REF_SEED_*.mat`, else ROI centre) |
| `ncorr_matching2ref_single.m` | Optional map of the reference *trial's* ROI/seed onto the current trial | `--filter`/`do_matching` hook (cross-trial mapping is a documented gap) |
| `main_analysis_vik.m` | Top-level batch loop (subject/bloc/trial table, forward+backward passes) | driven externally via repeated CLI invocations (`--dir forward|backward`, `--ncorr-number`) |

Contrast the **stereo** step D, `Tools/MultiDIC/main_script/stepD_2DDIC.m`
(C++: `cppxdic::StepDWorkflow`), which additionally runs matching-between-cameras
and a second tracking pass feeding stepE/stepF 3D reconstruction — none of which
`singledic` performs.

The external Vik tree (`matlab_code_vik/`) carries the same workflow with minor
parameter/path differences; the in-repo submodule copy is the authoritative
reference.

## C++ flow

1. **Import** `singledic::SingleDicWorkflow::importFrames` — opens a video file
   (`<data>/vid/<subject>/<bloc>/<trial>.mp4`, or a video inside that dir) or a
   numbered image folder (via `ncorr::discover_frames`). Honours
   `--start/--end/--jump`; `--dir backward` reverses the sequence (Vik `flip`).
2. **Saturate** `cppxdic::ImageProcessor::saturate` twice (high then low),
   mirroring `satur(satur(...))`.
3. **ROI** `cppxdic::ROIManager::loadOrCreateROI` — non-blocking.
4. **Filter (optional)** `applyBandpassFilter` + `computePercentileBoundaries` +
   `normalizeAndClamp` (`filter_like_ben`-style), enabled with `--filter`.
5. **Seed** `cppxdic::ROIManager::loadOrCreateSeed`.
6. **Track** one ncorr pass via `ncorr::NcorrSession` (in-memory; reference vs
   each current frame). DIC params come from `Config.step_d` (radius, spacing →
   `scalefactor = spacing+1`, threads).
7. **Write** `ncorr<n>.csv` (per-frame grid `u,v,corrcoef`) and `ncorr<n>.mat`
   (matio: `spacing`, `seed`, `Nframe`, and `U_k/V_k/C_k` grids).

## Path / Config adaptation

`singledic::SinglediConfig` **wraps** the production `Config` (it does not modify
`include/config.h`). It inherits every DIC/ncorr/visualisation tunable and
overlays a single-camera (subject/bloc/trial) sub-path scheme:

- input  `<Config.data_path>/vid/<subject>/<bloc>/<trial>`
- output `<Config.dic_path>/<subject>/<bloc>/<trial>/ncorr<n>.{mat,csv}`
- ROI    `<.../subject/bloc>/REF_MASK_<reftrial>.mat`
- seed   `<.../subject/bloc>/REF_SEED_<reftrial>_<dir>.mat`

`toBaseParameters()` maps this identity onto `cppxdic::BaseParameters`
(`material`←bloc, `phase`←direction, `stereopair`=1, `cam_1=cam_2=1`) so the
existing ImageProcessor/ROIManager helpers are reused unchanged.

## Usage

```bash
singledic \
  --data-path /data/shared/data --dic-path /data/shared/analysis \
  --subject S17 --bloc bloc1_1 --trial vid_23 --reftrial vid_5 \
  --start 55 --end 175 --jump 1 --dir forward --ncorr-number 2 \
  --gs-low 40 --gs-high 140 --filter --filt-low 50 --filt-high 200 \
  --dic-params dic_params.txt
```

Run twice (forward/backward, `--ncorr-number 1/2`) to mirror Vik's
two-pass per-trial scheme.

## Building (applied at integration)

Add the target to the root `CMakeLists.txt`, behind the existing
`BUILD_SINGLEDIC` option, linking the single-camera sources + the engine pieces:

```cmake
if(BUILD_SINGLEDIC)
    add_executable(singledic
        apps/singledic/main.cpp
        src/singledic/single_dic_workflow.cpp
        # engine pieces reused from production:
        src/image_processor.cpp
        src/roi_manager.cpp
        src/config.cpp
        src/mat_reader.cpp        # ROIManager::loadROIFromMat / loadSeedFromMat
        ${NCORR_SESSION_SOURCES}  # Tools/CppNCorr session.cpp + engine
    )
    target_include_directories(singledic PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/Tools/CppNCorr/include)
    target_link_libraries(singledic PRIVATE
        ${OpenCV_LIBS} ${MATIO_LIBRARY} ${HDF5_C_LIBRARIES}
        Eigen3::Eigen ncorr OpenMP::OpenMP_CXX)
    target_compile_definitions(singledic PRIVATE XDIC_MODE_CAMERAPAIRS)
endif()
```

The simplest robust option is to link the existing `cppxdic_core` static lib
*after extending it* with `src/image_processor.cpp`, `src/roi_manager.cpp`,
`src/mat_reader.cpp`, then `target_link_libraries(singledic PRIVATE
cppxdic_core ncorr)`. See the task report for the exact dependency list.
```
