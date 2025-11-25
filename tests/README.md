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

### 1. test_ncorr_bin

Analyzes a `.bin` file and provides detailed statistics about the data.

**Usage:**
```bash
./bin/test_ncorr_bin <path_to_bin_file>
```

**Example:**
```bash
./bin/test_ncorr_bin ../DIC3Dcombined_2Pairs_stitched.bin
```

**Output:**
- Basic information (frames, points, faces, pairs)
- Displacement data summary (DispVec and DispMgn)
- Correlation coefficient summary (FaceCorrComb)
- Per-frame statistics including:
  - Min, max, mean, median values
  - Number of zeros, NaNs, and infinities
  - Percentage breakdowns

This tool is useful for:
- Debugging data quality issues
- Understanding why visualization summaries show NaN or zero values
- Verifying data integrity after processing

### 2. test_ncorr_to_mat

Converts a `.bin` file to `.mat` format using the existing reader and writer implementations.

**Usage:**
```bash
./bin/test_ncorr_to_mat <path_to_bin_file> [output_mat_file]
```

**Examples:**
```bash
# Creates DIC3Dcombined_2Pairs_stitched.mat in the same location
./bin/test_ncorr_to_mat ../DIC3Dcombined_2Pairs_stitched.bin

# Specify custom output location
./bin/test_ncorr_to_mat input.bin output.mat
```

**Output:**
- MAT file compatible with MATLAB/xDIC
- File size comparison between binary and MAT formats
- Summary of converted data

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
