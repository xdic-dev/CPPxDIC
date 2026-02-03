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

### 4. Pipeline Integration Test Files

For complete pipeline validation, you need both MATLAB and C++ outputs from the same dataset:

```matlab
% After running complete MATLAB pipeline
matlab_3d = load('DIC3Dcombined_1Pairs_stitched.mat');
matlab_deform = load('DIC3DPPresults_1Pairs_cum_v1.mat');

% Save with standard names
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
