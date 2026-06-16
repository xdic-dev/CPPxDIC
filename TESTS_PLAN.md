# CPPXDIC — Test Plan (Section 5a)

> **STATUS: AWAITING CONFIRMATION.** This is a *plan only*. No test code has been written.
> Per the task spec, implementation (Section 5b) does not start until this plan is confirmed.
>
> **Open questions for the reviewer** (please answer before Section 5b):
> 1. **Test framework.** None is currently vendored. Options, in order of preference:
>    (a) a tiny in-tree assertion harness (zero new deps, matches the "prefer in-tree"
>    constraint); (b) GoogleTest; (c) Catch2. The spec says GoogleTest/Catch2 require
>    confirmation. **Which do you want?**
> 2. **Fixture dataset.** `ohtcfrp` lives at
>    `Tools/CppNCorr/test/examples/ohtcfrp/images` (13 PNG frames) — a CppNCorr 2D example,
>    not a stereo fingertip dataset. It exercises the 2D-DIC path (proxyncorr / singledic),
>    but **not** the stereo 3D reconstruction or deformation stages, which need calibration
>    `.mat` files + camera-pair videos. Is there a lightweight stereo fixture available, or
>    should 3D/E2E tests be marked `skip` until one is provided?
> 3. **Golden values.** Where should expected reference outputs come from — captured from a
>    known-good CPPXDIC run, or from the MATLAB xDIC reference (`Tools/MultiDIC`)?

---

## Conventions

- Each candidate test names the **function under test** (file:symbol), the **input**, and the
  **expected final output / invariant**.
- Pure/algorithmic functions are prioritised for unit tests (deterministic, no I/O).
- Tests that need OpenCV / matio / the DIC engine are flagged `[heavy]`.

---

## 1. Unit tests — individual functions worth isolating

### Configuration & parameters (`include/config.h`, `src/config.cpp`)
- `Config()` default construction → every field equals its documented compiled default
  (spot-check: `num_pair == 2`, `idx_frame_start == 10`, `data_format == "mat"`,
  `step_e.radius == 60`).
- `Config::parseBool` → `"true"/"1"/"yes"` (any case) → `true`; everything else → `false`.
- `Config::parseStringList` / `parseIntList` / `parseDoubleList` → comma-separated parsing,
  whitespace trimming, and graceful handling of malformed tokens (warn + skip).
- `Config::parseConfigValue` → returns trimmed value for matching key only; `""` for
  non-matching key or line without `=`.
- `Config::loadFromConfigFile` (override chain, Section 4) → loading a temp `.cfg` overriding
  a few keys leaves untouched keys at their compiled defaults and applies the overrides.
- **Override precedence** → defaults < unified config file < specific param file < CLI:
  build a temp config setting `subject_id=AAA`, a temp dic_params setting `subject_id=BBB`,
  then `overrideSubject("CCC")` → final `subject_id == "CCC"`; with only the config file
  → `"AAA"`; with config + dic_params → `"BBB"`.
- `Config::updateVariables` → derives `material` correctly from `material_id` /
  `frictional_conditions`.
- `Config::overrideSubject` / `overrideRefTrial` → set the corresponding field.

### xDIC mode guard (`include/xdic/xdic_mode.h`)
- `xdic::active_mode()` / `active_mode_name()` → in the default build return
  `Mode::CameraPairs` / `"camerapairs"` (compile-time; a static_assert-style check).

### Image-folder reader (`include/input/image_folder_reader.h`)
- `ImageFolderReader::listFrames` on a temp dir with mixed files → returns only image
  extensions, sorted; respects `skip_roi_ref` (excludes `roi.png`/`ref.png`); skips hidden
  files; returns empty for a non-existent dir.

### Delaunay triangulation (`include/delaunay_triangulation.h`)
- `DelaunayTriangulation::compute` on a known small point set (e.g. a unit square's 4 corners
  + centre) → expected triangle count / connectivity.
- `computeEdgeLengths` → correct Euclidean edge lengths for a fixed triangle.
- `filterByEdgeLength` → drops triangles whose max edge exceeds the threshold.
- `flipOrientation` → reverses winding order (vertex index order) per face.

### Surface stitching (`include/surface_stitching.h`)
- `computeEdgeLengths` (3D) → correct lengths for fixed 3D vertices.
- `computeMeshBoundary` → returns the boundary edges of a small known mesh (e.g. two-triangle
  quad → its 4 outer edges, no interior edge).
- `stitchPairsSimple` → concatenation invariants: combined vertex/face counts equal the sum of
  inputs; face indices remain valid after re-offset.

### Temporal filter (`include/temporal_filter.h`)
- `filterTime` on a constant signal → output ≈ input (DC preserved).
- `filterTime` on signal = DC + high-frequency component above `freq_filt` → high-frequency
  component attenuated, DC retained.
- Edge case: single-sample series → returned unchanged (no crash).

### Face isotropy (`include/face_isotropy.h`)
- `computeSingleFaceIsotropyIndex` on an equilateral triangle → maximal isotropy value;
  on a degenerate sliver triangle → low value (bounds check).
- `computeFaceIsotropyIndex` → returns one value per face, all within the index's valid range.

### Image processing (`include/image_processor.h`) `[heavy: OpenCV]`
- `ImageProcessor::saturate(cv::Mat, level, "high")` → pixels above `level` clamped; below
  unchanged. `"low"` variant → symmetric behaviour.
- `saturate(vector<cv::Mat>, level)` → applied element-wise to each frame.
- `applyBandpassFilter` → output preserves image dimensions/type; flat input → flat output.

### ROI/seed mapping (`include/roi_manager.h`)
- `ROIManager::mapPixel2Subset` / `mapSubset2Pixel` → round-trip identity for spacings that
  divide evenly; correct scaling otherwise.

### MATLAB-compat helpers (`include/matlab_functions.h`) `[heavy]`
- `matlab_imgaussfilt3` on a single-voxel impulse volume → Gaussian-shaped response with the
  configured spatial/temporal sigmas; sum of weights ≈ 1 (energy preserved).

### MAT round-trip (`include/mat_writer.h`, `include/mat_reader.h`) `[heavy: matio]`
- `MatWriter::writeSeedFile` then read back → seed `(x, y)` recovered.
- `MatWriter::writeROIMaskFile` then read back via `mat_reader` → mask dimensions and values
  recovered.

### Serializers (`include/data_serializer.h`) `[heavy]`
- For each format (bin / json / mat): `save*` then `load*` of a small synthetic
  `DIC2DPairResults` / `DIC3Dcombined` → fields recovered (round-trip equality);
  `fileExists` reflects presence.

---

## 2. Integration tests — pipeline stages

### IT-1: Calibration load (`src/mat_reader.cpp`) `[heavy]`
- Input: a DLT calibration `.mat`. Expect `DLTCalibrationData` with 11 DLT params, correct
  camera index, and non-empty `C3Dtrue`. (Needs a sample calibration file — see open Q2.)

### IT-2: Video ingestion → frames (`src/utils.cpp Utils::importRawVid`) `[heavy]`
- Input: a short 2-camera `.mp4` pair following the naming convention. Expect per-camera PNGs
  written under `tmp_frames/...`, frame count = `(end-start)/jump + 1`, single-channel (Red)
  output. (Needs a sample video pair — see open Q2.)

### IT-3: 2D DIC on `ohtcfrp` (`apps/proxyncorr`) `[heavy]`
- Input: `Tools/CppNCorr/test/examples/ohtcfrp/images` + its `roi.png`, ref = first frame.
- Run proxyncorr (sequential mode) → produces displacement (`u`,`v`) and strain
  (`exx`,`eyy`,`exy`) fields with the ROI's dimensions; correlation coefficients below the
  cutoff for the valid region. Assert against captured golden JSON (see open Q3).

### IT-4: Step D workflow (`src/step_d_workflow.cpp StepDWorkflow::execute`) `[heavy]`
- Input: protocol + ROI/seed for one trial/pair. Expect a matching-results file path, a
  non-empty trial index list, and success flag true. (Needs stereo fixture — Q2.)

### IT-5: DIC → 3D reconstruction (`src/dic_analysis.cpp dic3DReconstruction`) `[heavy]`
- Input: 2D DIC pair results + calibration. Expect `DIC3DpairResults` with consistent
  point/face counts and finite 3D coordinates. (Needs stereo fixture — Q2.)

### IT-6: 3D → deformation (`src/dic_analysis.cpp dicDeformationAnalysis`,
  `src/strain_computation.cpp`) `[heavy]`
- Input: combined 3D surface across frames. Expect a `DeformationResult` with director
  triads (`D1..D3`, `d1..d3`) orthonormal per point and finite strain values.

### IT-7: Stitch two pairs (`src/surface_stitching.cpp stitchPairsGeometric`) `[heavy]`
- Input: two overlapping `DIC3DpairResults`. Expect a single `DIC3Dcombined` with the seam
  handled (with `gapLogic`), valid face indices, and total points ≈ sum minus overlap.

---

## 3. End-to-end test — full run on `ohtcfrp`

### E2E-1: 2D pipeline on `ohtcfrp` (achievable today) `[heavy]`
- Command: `proxyncorr --folder Tools/CppNCorr/test/examples/ohtcfrp/images
  --roi .../roi.png --mode sequential --output <tmp>`.
- Expected outputs: `<tmp>/save/{DIC,strain}_*.bin`, `<tmp>/save_json/*.json`, overlay videos.
- Assertions: process exit 0; output files exist and are non-empty; the final-frame `v`
  displacement field's mean/median falls within a tolerance band of a captured golden value
  (regression guard, not bit-exactness).

### E2E-2: Full stereo fingertip pipeline (`cppxdic` camerapairs) — **likely skipped in CI**
- Command: `cppxdic --config config/default.cfg` against a full subject dataset (videos +
  calibration + protocol + ROI/seed `.mat`).
- Expected: completes through stepD → stepE → stepF, writing combined 3D + deformation files
  for the configured frame range; exit 0.
- **Skip reason (to be documented in CI):** requires the large multi-camera dataset +
  calibration that is not lightweight enough for CI and is not present in-repo. Run locally /
  nightly only. Needs the stereo fixture from open Q2 to be runnable at all.

---

## Coverage summary

| Layer        | Candidates | Runnable now with `ohtcfrp` | Needs stereo fixture / golden data |
|--------------|-----------:|:---------------------------:|:----------------------------------:|
| Unit         | ~25        | most (pure functions)       | a few (`[heavy]` MAT/serializer)   |
| Integration  | 7          | IT-3                        | IT-1,2,4,5,6,7                     |
| End-to-end   | 2          | E2E-1                       | E2E-2                              |

**Recommended first slice for Section 5b** (no new heavy deps, no stereo fixture):
all pure-function unit tests (config, mode guard, image-folder reader, delaunay, temporal
filter, face isotropy, stitching math) + IT-3 + E2E-1 on `ohtcfrp`. The remaining heavy/stereo
tests are deferred until a fixture (Q2) and golden data (Q3) are provided.
