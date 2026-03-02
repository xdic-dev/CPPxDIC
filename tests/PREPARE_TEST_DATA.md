# Preparing MATLAB Reference Data for Validation Tests

This guide explains how to prepare MATLAB reference data files for validating the C++ DIC implementation against the original MATLAB MultiDIC toolbox.

## Overview

The validation tests compare C++ outputs against MATLAB reference data to ensure numerical accuracy and functional correctness. Each test requires specific MAT files with the expected data structures.

## Required Reference Files

### 1. Deformation Test Reference (`deformation_reference.mat`)

Create this file from MATLAB after running the deformation analysis:

```matlab
% Run your MATLAB DIC analysis first to get DIC3DPPresults
load('path/to/your/DIC3DPPresults.mat');

% Extract test data
Faces = DIC3DPPresults.Faces;
vertices_ref = DIC3DPPresults.Points3D{1};  % Reference frame vertices
vertices_def = DIC3DPPresults.Points3D;     % All frames

% Extract deformation results
Deform = DIC3DPPresults.Deform;

% Save reference file
save('test_data/deformation_reference.mat', 'Faces', 'vertices_ref', 'vertices_def', 'Deform', '-v7.3');
```

### 2. Image Filter Test Reference (`filter_reference.mat`)

Prepare test data for Ben's filter validation:

```matlab
% Load test images (e.g., from Step D)
input_images = {};  % Cell array of images
for i = 1:10
    img = imread(sprintf('frame_%03d.tif', i));
    input_images{i} = double(img);
end

% Load or create mask
mask = logical(imread('roi_mask.tif'));

% Set filter parameters
param_filt = [25, 300];  % [radius, cutoff]

% Apply filter in MATLAB
[filtered_images, gs_boundaries] = filter_like_ben(input_images, 'mask', mask, 'paramfilt', param_filt);

% Save reference
save('test_data/filter_reference.mat', 'input_images', 'mask', 'param_filt', ...
     'filtered_images', 'gs_boundaries', '-v7.3');
```

### 3. Temporal Filter Test Reference (`temporal_reference.mat`)

Create reference data for temporal filtering:

```matlab
% Generate test displacement data (n_frames x n_points)
n_frames = 100;
n_points = 500;
disp_x = randn(n_frames, n_points);
disp_y = randn(n_frames, n_points);
disp_z = randn(n_frames, n_points);

% Apply temporal filter in MATLAB
freq_filt = 10;  % Cutoff frequency (Hz)
freq_acq = 50;   % Acquisition frequency (Hz)

[filt_x, filt_y, filt_z] = myfilterTime(disp_x, disp_y, disp_z, freq_filt, freq_acq);

% Save reference
save('test_data/temporal_reference.mat', 'disp_x', 'disp_y', 'disp_z', ...
     'filt_x', 'filt_y', 'filt_z', 'freq_filt', 'freq_acq', '-v7.3');
```

### 4. Surface Stitching Unit Tests (No Data Required)

`test_unit_stitching` uses synthetic meshes — no MATLAB reference data needed. It validates:
- `computeMeshBoundary` — boundary edge detection
- `computeEdgeLengths` — edge length calculation
- `groupBoundaryEdges` — connected component grouping
- `edgeListToCurve` — ordered boundary curve construction
- `findAllBoundaryFaces` — all-boundary-edge face detection
- `zipBoundaryCurves` — greedy boundary zipping
- `removeOverlapSurfaces` — centroid-based overlap removal
- `stitchPairsSimple` — append-based multi-pair stitching
- `stitchPairsGeometric` — geometric stitching with overlap removal

Just build and run:
```bash
./bin/test_unit_stitching
```

### 5. 3D Reconstruction Integration Test

Calls `DicAnalysis::runStepE()` directly to run the full C++ 3D reconstruction
pipeline on MATLAB Step D inputs, then compares the generated `.mat` output
against a MATLAB reference.

#### Directory layout

Create a test root directory (e.g. `test_data/reconstruction/`) with this
structure:

```
test_data/reconstruction/
  test_config.txt
  dic_output/<subject>/<material>/<trial>/<phase>/.cache/
    cam1_output.bin          <- Step D binary outputs (from MATLAB or C++)
    cam2_output.bin
    matching_output.bin
  dic_output/<subject>/<material>/
    DLTcoefficients_<N>.csv  <- Calibration files (one per camera pair)
  matlab_reference/
    DIC3Dcombined_matlab.mat <- MATLAB Step 3 output
```

#### test_config.txt

Minimal config overrides (see `config.h` for all parameters):

```
subject_id = S09
material = glass
num_pair = 2
ref_trial_id = 5
phase_id = loading
dic_path = <test_root>/dic_output
generate_mat_files = true
```

#### Prepare MATLAB reference

```matlab
% After running MATLAB pipeline Step 3 (step3_dic_rewrited)
matlab_3d = load('DIC3Dcombined_2Pairs_stitched.mat');
save('test_data/reconstruction/matlab_reference/DIC3Dcombined_matlab.mat', ...
     '-struct', matlab_3d, '-v7.3');
```

Also copy the Step D `.bin` cache files and `DLTcoefficients_*.csv` calibration
files into the directory structure shown above.

#### Run the test

```bash
./bin/test_reconstruction_integration test_data/reconstruction/ 1e-4
```

The test will:
1. Remove any existing Step E output to force re-computation
2. Call `DicAnalysis::runStepE()` to generate `DIC3Dcombined_*.mat`
3. Compare generated output against the MATLAB reference

Fields compared: `Points3D` (x/y/z per frame), `Faces`, `FaceColors`,
`corrComb`, `FaceCorrComb`, `DispMgn`, `FacePairInds`.

### 6. Deformation Integration Test

Calls `DicAnalysis::runStepF()` directly to run the full C++ TCPE deformation
pipeline, then compares the generated `.mat` output against a MATLAB reference.

#### Directory layout

```
test_data/deformation/
  test_config.txt
  dic_output/<subject>/<material>/
    DIC3Dcombined_<N>Pairs_stitched.bin  <- (auto-created from .mat if absent)
  matlab_reference/
    DIC3Dcombined_matlab.mat             <- MATLAB Step 3 output (input to Step F)
    DIC3DPPresults_matlab.mat            <- MATLAB Step 4 output (reference)
```

**Note:** If the `.bin` file does not exist, the test automatically converts
`DIC3Dcombined_matlab.mat` to `.bin` format at the expected path.

#### test_config.txt

Same format as the reconstruction test:

```
subject_id = S09
material = glass
num_pair = 2
ref_trial_id = 5
dic_path = <test_root>/dic_output
generate_mat_files = true
```

#### Prepare MATLAB reference

```matlab
% After running MATLAB pipeline Step 3 + Step 4
matlab_3d = load('DIC3Dcombined_2Pairs_stitched.mat');
save('test_data/deformation/matlab_reference/DIC3Dcombined_matlab.mat', ...
     '-struct', matlab_3d, '-v7.3');

matlab_deform = load('DIC3DPPresults_2Pairs_cum_v1.mat');
save('test_data/deformation/matlab_reference/DIC3DPPresults_matlab.mat', ...
     '-struct', matlab_deform, '-v7.3');
```

#### Run the test

```bash
./bin/test_deformation_integration test_data/deformation/ 1e-6
```

The test will:
1. Convert MATLAB `.mat` → `.bin` if the binary input is missing
2. Remove any existing Step F output to force re-computation
3. Call `DicAnalysis::runStepF()` to generate `DIC3DPPresults_*.mat`
4. Compare generated output against the MATLAB reference

Fields compared: `Epc1`, `Epc2`, `epc1`, `epc2`, `EShearMax`, `eShearMax`,
`Eeq`, `eeq`, `Emgn`, `emgn`, `J`, `Lamda1`, `Lamda2` (first and last frame).

**Note:** Both integration tests run the actual C++ pipeline functions, so they
validate the full code path (loading, computation, output) against MATLAB —
not just isolated algorithms.

### 7. Generic Pipeline MAT Comparison

For generic structure-level comparison between any two MAT files:

```matlab
% After running complete MATLAB pipeline
matlab_3d = load('DIC3Dcombined_1Pairs_stitched.mat');
matlab_deform = load('DIC3DPPresults_1Pairs_cum_v1.mat');

save('test_data/DIC3Dcombined_matlab.mat', '-struct', matlab_3d, '-v7.3');
save('test_data/DIC3DPPresults_matlab.mat', '-struct', matlab_deform, '-v7.3');
```

Then run the C++ pipeline on the same dataset and copy the outputs:

```bash
cp /path/to/cpp/DIC3Dcombined_1Pairs_stitched.mat test_data/DIC3Dcombined_cpp.mat
cp /path/to/cpp/DIC3DPPresults_1Pairs_cum_v1.mat test_data/DIC3DPPresults_cpp.mat
```

## Data Format Requirements

### Points3D Structure
```matlab
Points3D = {
    struct('x', [...], 'y', [...], 'z', [...]),  % Frame 1
    struct('x', [...], 'y', [...], 'z', [...]),  % Frame 2
    ...
};
```

### Deform Structure
```matlab
Deform = struct(
    'Epc1', {...},      % Cell array of principal strain 1
    'Epc2', {...},      % Cell array of principal strain 2
    'epc1', {...},      % Cell array of engineering principal strain 1
    'epc2', {...},      % Cell array of engineering principal strain 2
    'EShearMax', {...}, % Cell array of max shear strain
    'Eeq', {...},       % Cell array of equivalent strain
    'J', {...},         % Cell array of Jacobian
    'Lamda1', {...},    % Cell array of principal stretch 1
    'Lamda2', {...},    % Cell array of principal stretch 2
    'Fmat', {...},      % Cell array of deformation gradient tensors
    'Cmat', {...},      % Cell array of right Cauchy-Green tensors
    'Emat', {...},      % Cell array of Green-Lagrange strain tensors
    ...
);
```

## Running Validation Tests

Once reference data is prepared:

1. Place all MAT files in `tests/test_data/` directory
2. Build the test executables:
   ```bash
   cd tests
   mkdir -p build && cd build
   cmake ..
   make -j4
   cd ..
   ```
3. Run validation tests:
   ```bash
   chmod +x run_validation_tests.sh
   ./run_validation_tests.sh
   ```

## Tolerance Levels

The tests use different tolerance levels for numerical comparisons:

- **EXACT** (1e-12): For integer values like face connectivity
- **STRICT** (1e-10): For values that should match exactly
- **NORMAL** (1e-6): For general floating-point comparisons
- **RELAXED** (1e-3): For accumulated errors in iterative computations

## Interpreting Results

Test reports include:
- **Pass/Fail status** for each field
- **Maximum error** encountered
- **Mean error** and **RMSE** statistics
- **Detailed failure messages** for first few failures
- **Recommendations** based on failure patterns

A successful validation shows:
```
✓ All validation tests PASSED
Tests Passed: 6/6 (100%)
```

Failures are reported with details:
```
Test: TCPE Deformation
Status: FAILED
Field: Epc1
  Max Error: 2.3e-5
  Tolerance: 1e-6
  Failures: 23/1000 (2.3%)
```

## Troubleshooting

### Common Issues

1. **Size mismatch**: Ensure C++ and MATLAB process same number of frames
2. **Indexing differences**: MATLAB uses 1-based, C++ uses 0-based indexing
3. **Column-major vs row-major**: MATLAB stores matrices column-major
4. **NaN handling**: Check for invalid triangles or divide-by-zero

### Debug Mode

For detailed debugging, modify test source to increase verbosity:

```cpp
// In test files, increase error reporting limit
if (result.failure_details.size() < 100) {  // Was 5
    // Log more failures
}
```

## Continuous Integration

For CI/CD pipelines, the test runner returns appropriate exit codes:
- 0: All tests passed
- 1: One or more tests failed

This can be integrated into GitHub Actions or other CI systems:

```yaml
- name: Run DIC Validation Tests
  run: |
    cd tests
    ./run_validation_tests.sh
```
