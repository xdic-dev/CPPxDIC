# Implementation TODO List

## Phase 1: Core Deformation Field Writing (HIGH PRIORITY)

### Task 1.1: Implement Cell Array Helper Functions
**File**: `src/mat_writer.cpp`, `include/mat_writer.h`

**Add helper functions**:
```cpp
// Create cell array from vector of matrices
static matvar_t* createCellArrayFromMatrices(
    const std::string& name,
    const std::vector<Eigen::Matrix3d>& matrices,
    size_t n_frames);

// Create cell array from vector of vectors
static matvar_t* createCellArrayFromVectors(
    const std::string& name,
    const std::vector<Eigen::Vector3d>& vectors,
    size_t n_frames);

// Create cell array from vector of scalars
static matvar_t* createCellArrayFromScalars(
    const std::string& name,
    const std::vector<double>& scalars,
    size_t n_frames);
```

**Estimated Time**: 2-3 hours

---

### Task 1.2: Write All Deformation Fields to Deform/ Group
**File**: `src/mat_writer.cpp` - `write3DPPresults()`

**Implementation**:
1. Create HDF5 group "Deform" under DIC3DPPresults
2. For each of the 36 deformation measures, write as cell array (1x150):
   - **Tensors (3x3 matrices)**: Fmat, Cmat, Emat, emat
   - **Vectors (3x1)**: D1, D2, D3, d1, d2, d3, Drec1, Drec2, Dnorm
   - **Vectors (3x1)**: Epc1vec, Epc2vec, Epc1vecCur, Epc2vecCur
   - **Vectors (3x1)**: epc1vec, epc2vec
   - **Vectors (3x1)**: EShearMaxVec1, EShearMaxVec2, EShearMaxVecCur1, EShearMaxVecCur2
   - **Vectors (3x1)**: eShearMaxVec1, eShearMaxVec2
   - **Scalars**: Lamda1, Lamda2, J, Area
   - **Scalars**: Emgn, emgn, Epc1, Epc2, epc1, epc2
   - **Scalars**: EShearMax, eShearMax, Eeq, eeq

**Reference**: `strain_computation.cpp` - `DeformationResult` structure

**Estimated Time**: 4-5 hours

---

### Task 1.3: Write Disp/ Group
**File**: `src/mat_writer.cpp` - `write3DPPresults()`

**Implementation**:
1. Create HDF5 group "Disp" under DIC3DPPresults
2. Write DispVec as cell array (150x1):
   - Each cell contains Nx3 array (N points, 3D displacement)
3. Write DispMgn as cell array (150x1):
   - Each cell contains Nx1 array (N points, magnitude)

**Data Source**: `DIC3DPPresults::Disp` from `dic_structures.h`

**Estimated Time**: 2 hours

---

### Task 1.4: Write Face-Based Cell Arrays
**File**: `src/mat_writer.cpp` - `write3DPPresults()`

**Implementation**:
1. Write FaceCentroids as cell array (150x1):
   - Each cell contains Mx3 array (M faces, 3D centroid)
2. Write FaceCorrComb as cell array (150x1):
   - Each cell contains Mx1 array (M faces, correlation)
3. Write FaceIsoInd as cell array (150x1):
   - Each cell contains Mx1 array (M faces, isotropy index)

**Data Source**: `DIC3DPPresults` fields

**Estimated Time**: 2 hours

---

## Phase 2: Calibration and Metadata (MEDIUM PRIORITY)

### Task 2.1: Implement calibration/ Group
**File**: `src/mat_writer.cpp` - `write3DCombinedResults()`

**Implementation**:
1. Create HDF5 group "calibration"
2. Write DLTparameters as object 2d array (2x2):
   - Cell array with DLT parameters for each camera pair
3. Write DLTpath as object 2d array (2x2):
   - Cell array with paths to DLT files

**Data Source**: `DIC3Dcombined::calibration` from `dic_structures.h`

**Estimated Time**: 2 hours

---

### Task 2.2: Implement distortion/ Group
**File**: `src/mat_writer.cpp` - `write3DCombinedResults()`

**Implementation**:
1. Create HDF5 group "distortion"
2. Write distortionModel as object 2d array (2x2):
   - Cell array with distortion model names
3. Write distortionPath as object 2d array (2x2):
   - Cell array with paths to distortion files

**Data Source**: `DIC3Dcombined::distortion` from `dic_structures.h`

**Estimated Time**: 1.5 hours

---

### Task 2.3: Implement AllPairsResults
**File**: `src/mat_writer.cpp` - `write3DCombinedResults()`

**Implementation**:
1. Write AllPairsResults as cell array (1xN):
   - Each cell contains complete DIC3Dcombined structure for one pair
2. Recursively write each pair's data

**Data Source**: `DIC3Dcombined::AllPairsResults`

**Estimated Time**: 3 hours

---

### Task 2.4: Implement DIC2Dinfo
**File**: `src/mat_writer.cpp` - `write3DCombinedResults()`

**Implementation**:
1. Write DIC2Dinfo as object array (2x1 or Nx1):
   - Each element is a DIC2DPairResults structure
2. Include all fields from myDIC2DpairResults

**Data Source**: `DIC3Dcombined::DIC2Dinfo`

**Estimated Time**: 2 hours

---

## Phase 3: Fix myDIC2DpairResults (MEDIUM PRIORITY)

### Task 3.1: Fix CorCoeffVec Cell Array
**File**: `src/mat_writer.cpp` - `writeDIC2DPairResults()`

**Implementation**:
1. Change from single vector to cell array (300x1)
2. Each cell contains correlation coefficients for one frame (Nx1 array)

**Current Issue**: Writing as single vector instead of cell array

**Estimated Time**: 1.5 hours

---

### Task 3.2: Fix Points Cell Array
**File**: `src/mat_writer.cpp` - `writeDIC2DPairResults()`

**Implementation**:
1. Write Points as cell array (300x1)
2. Each cell contains Nx2 array (N points, [x, y] coordinates)

**Current Issue**: Writing as struct instead of proper 2D arrays

**Estimated Time**: 1.5 hours

---

### Task 3.3: Implement ncorrInfo Structure
**File**: `src/mat_writer.cpp` - `writeDIC2DPairResults()`

**Implementation**:
1. Create ncorrInfo struct with all fields:
   - cutoff_corrcoef (float64 1d array 301)
   - cutoff_diffnorm (float)
   - cutoff_iteration (int)
   - imgcorr (cell array len=2)
   - lenscoef, pixtounits, radius, spacing (int)
   - stepanalysis (struct)
   - subsettrunc (int)
   - total_threads (int)
   - type (string)
   - units (string)

**Data Source**: `DIC2DPairResults::ncorrInfo` from `dic_structures.h`

**Estimated Time**: 2.5 hours

---

## Phase 4: Fix ncorr Files (MEDIUM PRIORITY)

### Task 4.1: Fix current_save/gs Cell Array
**File**: `src/mat_writer.cpp` - `writeDicNcorrFile()`

**Implementation**:
1. For multi-frame ncorr files (ncorr1, ncorr2, etc.):
   - Write current_save/gs as cell array (150x1)
   - Each cell contains one grayscale image

**Current Issue**: Writing as single image for multi-frame tracking

**Estimated Time**: 2 hours

---

## Phase 5: Testing Infrastructure (HIGH PRIORITY)

### Task 5.1: Create MAT File Comparator Test
**File**: `tests/src/test_mat_comparator.cpp`

**Implementation**:
1. Load MATLAB reference MAT file
2. Load C++ generated MAT file
3. Compare structure hierarchy
4. Compare data types
5. Compare array dimensions
6. Compare data values (with tolerance)
7. Generate detailed diff report

**Features**:
- Recursive structure comparison
- Cell array validation
- HDF5 group traversal
- Numerical tolerance for floating point
- Pretty-printed diff output

**Estimated Time**: 6-8 hours

---

### Task 5.2: Create Unit Tests for Each MAT File Type
**File**: `tests/src/test_mat_writers.cpp`

**Implementation**:
1. Test writeMatchingFile()
2. Test writeROIMaskFile()
3. Test writeSeedFile()
4. Test writeTrialInfoFile()
5. Test writeDIC2DPairResults()
6. Test write3DCombinedResults()
7. Test write3DPPresults()

**Estimated Time**: 4-5 hours

---

## Phase 6: Documentation and Validation (LOW PRIORITY)

### Task 6.1: Update Documentation
**Files**: `README.md`, `docs/MAT_FILE_FORMAT.md`

**Content**:
1. Document all MAT file structures
2. Add examples of reading files in MATLAB
3. Add examples of reading files in Python (scipy.io)
4. Document differences from MATLAB MultiDIC

**Estimated Time**: 3 hours

---

### Task 6.2: Create Validation Script
**File**: `scripts/validate_mat_outputs.py`

**Implementation**:
1. Python script to validate all MAT files
2. Check against reference MATLAB outputs
3. Generate validation report
4. Integration with CI/CD

**Estimated Time**: 4 hours

---

## Total Estimated Time

| Phase | Tasks | Estimated Time |
|-------|-------|----------------|
| Phase 1: Core Deformation | 4 tasks | 10-12 hours |
| Phase 2: Calibration/Metadata | 4 tasks | 8.5 hours |
| Phase 3: Fix myDIC2DpairResults | 3 tasks | 5.5 hours |
| Phase 4: Fix ncorr Files | 1 task | 2 hours |
| Phase 5: Testing | 2 tasks | 10-13 hours |
| Phase 6: Documentation | 2 tasks | 7 hours |
| **TOTAL** | **16 tasks** | **43-48 hours** |

---

## Implementation Order (Recommended)

1. **Task 1.1** - Cell array helpers (foundation)
2. **Task 1.2** - Deformation fields (core functionality)
3. **Task 1.3** - Disp/ group (core functionality)
4. **Task 1.4** - Face-based arrays (core functionality)
5. **Task 5.1** - MAT comparator (validation)
6. **Task 3.1-3.3** - Fix myDIC2DpairResults (important for 2D->3D pipeline)
7. **Task 2.1-2.4** - Calibration/metadata (completeness)
8. **Task 4.1** - Fix ncorr files (polish)
9. **Task 5.2** - Unit tests (quality assurance)
10. **Task 6.1-6.2** - Documentation (final polish)

---

## Dependencies

```
Task 1.1 (helpers) → Task 1.2, 1.3, 1.4, 3.1, 3.2
Task 1.2, 1.3, 1.4 → Task 5.1 (need output to test)
Task 5.1 → All other tasks (validation framework)
```

---

## Notes

- All tasks assume familiarity with matio library and HDF5 format
- MATLAB reference files needed for validation (from `/Users/jaoga/devlab/MultiDIC/example_data/analysis/test/S09/007`)
- Consider using MATLAB's `h5disp()` and `h5info()` for structure inspection
- Python's `h5py` can also be used for validation

