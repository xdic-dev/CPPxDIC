# CPPxDIC Test Utilities

This directory contains test utilities for analyzing and converting NCORR binary files.

## Building the Tests

```bash
cd tests
mkdir -p build
cd build
cmake ..
make
```

The compiled binaries will be placed in `tests/bin/`.

## Test Programs

Both test programs support **two binary file formats** with automatic detection:
- **ncorr DIC_analysis_output files** (`ncorr*.mat.bin`) - Intermediate 2D DIC results
- **DIC3Dcombined files** (`DIC3Dcombined*.bin`) - Final 3D stitched results

### 1. test_ncorr_bin

Analyzes binary files and provides detailed statistics about the data.

**Supported Formats:**
- `ncorr*.mat.bin` - 2D DIC displacement fields (u, v, cc) from ncorr tracking
- `DIC3Dcombined*.bin` - 3D reconstructed points and displacements

**Usage:**
```bash
./bin/test_ncorr_bin <path_to_bin_file>
```

**Examples:**
```bash
# Analyze ncorr 2D tracking results
./bin/test_ncorr_bin /path/to/.cache/ncorr1.mat.bin

# Analyze 3D combined results
./bin/test_ncorr_bin ../DIC3Dcombined_2Pairs_stitched.bin
```

**Output for ncorr files:**
- Basic information (frames, perspective, units, field dimensions)
- Per-frame statistics for U, V, and correlation coefficient including:
  - Min, max, mean, median values
  - Number of zeros, NaNs, and infinities
  - Percentage breakdowns

**Output for DIC3Dcombined files:**
- Basic information (frames, points, faces, pairs)
- Displacement data summary (DispVec and DispMgn)
- Correlation coefficient summary (FaceCorrComb)
- Per-frame statistics with data quality metrics

**Use Cases:**
- Debugging data quality issues
- Understanding why visualization summaries show NaN or zero values
- Verifying data integrity after processing
- Analyzing intermediate ncorr tracking results

### 2. test_ncorr_to_mat

Converts binary files to MATLAB `.mat` format for analysis.

**Supported Formats:**
- `ncorr*.mat.bin` → `.mat` with u, v, cc cell arrays + metadata
- `DIC3Dcombined*.bin` → `.mat` with full 3D structure (xDIC compatible)

**Usage:**
```bash
./bin/test_ncorr_to_mat <path_to_bin_file> [output_mat_file]
```

**Examples:**
```bash
# Convert ncorr 2D results (creates ncorr1.mat)
./bin/test_ncorr_to_mat /path/to/.cache/ncorr1.mat.bin

# Convert 3D combined results with custom output
./bin/test_ncorr_to_mat DIC3Dcombined.bin output.mat
```

**Output for ncorr files:**
- MAT file containing:
  - `u{1:nFrames}` - Horizontal displacement fields (HxW matrices)
  - `v{1:nFrames}` - Vertical displacement fields (HxW matrices)
  - `cc{1:nFrames}` - Correlation coefficient fields (HxW matrices)
  - `perspective` - "Lagrangian" or "Eulerian"
  - `units` - Measurement units (e.g., "mm")
  - `units_per_pixel` - Spatial calibration

**Output for DIC3Dcombined files:**
- MAT file compatible with MATLAB/xDIC containing full 3D structure
- File size comparison between binary and MAT formats

## Directory Structure

```
tests/
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── src/                    # Test source code
│   ├── test_ncorr_bin.cpp
│   └── test_ncorr_to_mat.cpp
├── bin/                    # Compiled binaries (created after build)
└── build/                  # Build directory (created by user)
```

## Troubleshooting

### NaN Displacement Values

If you see all NaN values in displacement statistics:
1. Use `test_ncorr_bin` to analyze the data
2. Check if DispMgn or DispVec arrays are empty
3. Verify the binary file was created correctly in the previous processing step

### Zero Correlation Coefficients

If you see zeros in correlation coefficients (except frame 0):
1. Check the FaceCorrComb data with `test_ncorr_bin`
2. Verify the correlation data was computed during DIC analysis
3. Check if only the first frame has valid correlation data

## Dependencies

These test utilities require:
- OpenCV
- matio (for MAT file I/O)
- ncorr library (for test_ncorr_to_mat only)

All dependencies should already be configured in your main CPPxDIC project.
