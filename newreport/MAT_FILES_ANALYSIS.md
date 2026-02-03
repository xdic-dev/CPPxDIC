# MAT Files Analysis and Implementation Status

## Overview
This document analyzes all expected MAT file outputs for a trial and compares them with the current C++ implementation.

## Expected MAT Files for Trial (from mat_trees.txt)

### 1. **DIC3DPPresults_2Pairs_cum_v1.mat** (HDF5 v7.3)
**Status**: ⚠️ PARTIALLY IMPLEMENTED

**Expected Structure**:
```
DIC3DPPresults/
├── AllPairsResults (object 2d array 1x2)
├── DIC2Dinfo (object 2d array 2x1)
├── Deform/ (group)
│   ├── Area (object 2d array 1x150)
│   ├── Cmat (object 2d array 1x150)
│   ├── EShearMax (object 2d array 1x150)
│   ├── EShearMaxVec1 (object 2d array 1x150)
│   ├── EShearMaxVec2 (object 2d array 1x150)
│   ├── EShearMaxVecCur1 (object 2d array 1x150)
│   ├── EShearMaxVecCur2 (object 2d array 1x150)
│   ├── Eeq (object 2d array 1x150)
│   ├── Emat (object 2d array 1x150)
│   ├── Emgn (object 2d array 1x150)
│   ├── Epc1 (object 2d array 1x150)
│   ├── Epc1vec (object 2d array 1x150)
│   ├── Epc1vecCur (object 2d array 1x150)
│   ├── Epc2 (object 2d array 1x150)
│   ├── Epc2vec (object 2d array 1x150)
│   ├── Epc2vecCur (object 2d array 1x150)
│   ├── Fmat (object 2d array 1x150)
│   ├── J (object 2d array 1x150)
│   ├── Lamda1 (object 2d array 1x150)
│   ├── Lamda2 (object 2d array 1x150)
│   ├── d3 (object 2d array 1x150)
│   ├── eShearMax (object 2d array 1x150)
│   ├── eShearMaxVec1 (object 2d array 1x150)
│   ├── eShearMaxVec2 (object 2d array 1x150)
│   ├── eeq (object 2d array 1x150)
│   ├── emat (object 2d array 1x150)
│   ├── emgn (object 2d array 1x150)
│   ├── epc1 (object 2d array 1x150)
│   ├── epc1vec (object 2d array 1x150)
│   ├── epc2 (object 2d array 1x150)
│   └── epc2vec (object 2d array 1x150)
├── Disp/ (group)
│   ├── DispMgn (object 2d array 150x1)
│   └── DispVec (object 2d array 150x1)
├── FaceCentroids (object 2d array 150x1)
├── FaceColors (float64 2d array 1x12371)
├── FaceCorrComb (object 2d array 150x1)
├── FaceIsoInd (object 2d array 150x1)
├── FacePairInds (float64 2d array 1x12371)
├── Faces (float64 2d array 3x12371)
├── PointPairInds (float64 2d array 1x6730)
├── Points3D (object 2d array 150x1)
├── calibration/ (group)
│   ├── DLTparameters (object 2d array 2x2)
│   └── DLTpath (object 2d array 2x2)
├── corrComb (object 2d array 150x1)
├── distortion/ (group)
│   ├── distortionModel (object 2d array 2x2)
│   └── distortionPath (object 2d array 2x2)
└── pairIndices (float64 2d array 2x2)
```

**Current Implementation**:
- ✅ Basic structure in `write3DPPresults()` 
- ✅ Deformation computation in `strain_computation.cpp`
- ❌ **MISSING**: All 36 deformation fields are NOT being written to MAT file
- ❌ **MISSING**: Cell array format for time-series data (1x150 frames)
- ❌ **MISSING**: Proper HDF5 group structure for Deform/
- ❌ **MISSING**: Disp/ group with DispMgn and DispVec
- ❌ **MISSING**: FaceCentroids, FaceCorrComb, FaceIsoInd as cell arrays
- ❌ **MISSING**: calibration/ and distortion/ groups
- ❌ **MISSING**: AllPairsResults and DIC2Dinfo

---

### 2. **DIC3Dcombined_2Pairs_stitched.mat** (HDF5 v7.3)
**Status**: ⚠️ PARTIALLY IMPLEMENTED

**Expected Structure**:
```
DIC3Dcombined/
├── AllPairsResults (object 2d array 1x2)
├── DIC2Dinfo (object 2d array 2x1)
├── FaceColors (float64 2d array 1x12371)
├── FacePairInds (float64 2d array 1x12371)
├── Faces (float64 2d array 3x12371)
├── PointPairInds (float64 2d array 1x6730)
├── Points3D (object 2d array 150x1)  # Cell array with 150 frames
├── calibration/ (group)
│   ├── DLTparameters (object 2d array 2x2)
│   └── DLTpath (object 2d array 2x2)
├── corrComb (object 2d array 150x1)  # Cell array
├── distortion/ (group)
│   ├── distortionModel (object 2d array 2x2)
│   └── distortionPath (object 2d array 2x2)
└── pairIndices (float64 2d array 2x2)
```

**Current Implementation**:
- ✅ Basic structure in `write3DCombinedResults()`
- ❌ **MISSING**: AllPairsResults and DIC2Dinfo
- ❌ **MISSING**: calibration/ and distortion/ groups
- ❌ **MISSING**: Proper cell array format for Points3D (currently writes as struct)
- ❌ **MISSING**: corrComb as cell array

---

### 3. **MATCHING2005_pair1.mat** (HDF5 v7.3)
**Status**: ✅ IMPLEMENTED

**Expected Structure**:
```
├── current_save/ (group)
│   ├── gs (object 2d array 2x1)
│   ├── name, path, roi, type
├── data_dic_save/ (group)
│   ├── dispinfo/ (group)
│   └── displacements/ (group)
└── reference_save/ (group)
    ├── gs (float64 2d array 1936x1216)
    ├── name, path, roi, type
```

**Current Implementation**:
- ✅ Fully implemented in `writeMatchingFile()` and `writeDicNcorrFile()`

---

### 4. **MATCHING2005_pair2.mat** (HDF5 v7.3)
**Status**: ✅ IMPLEMENTED (same as pair1)

---

### 5. **dic_info_data_target_pair1.mat** (MAT v5)
**Status**: ✅ IMPLEMENTED

**Expected Structure**:
```
├── actual_fps_meas (int)
└── idxframe (uint8 1d array 150)
```

**Current Implementation**:
- ✅ Fully implemented in `writeTrialInfoFile()`

---

### 6. **dic_info_data_target_pair2.mat** (MAT v5)
**Status**: ✅ IMPLEMENTED (same as pair1)

---

### 7. **myDIC2DpairResults_C_1_C_2.mat** (MAT v5)
**Status**: ⚠️ PARTIALLY IMPLEMENTED

**Expected Structure**:
```
DIC2DpairResults/
├── CorCoeffVec (cell array len=300, shape=(300,))
│   ├── [0] (float64 1d array 3651)
│   ├── [1] (float64 1d array 3651)
│   └── ... [299]
├── FaceColors (float64 1d array 7088)
├── Faces (uint16 2d array 7088x3)
├── Points (cell array len=300, shape=(300,))
│   ├── [0] (float64 2d array 3651x2)
│   └── ... [299]
├── ROImask (uint8 2d array 1216x1936)
├── nCamDef (int)
├── nCamRef (int)
├── nImages (int)
└── ncorrInfo (struct)
    ├── cutoff_corrcoef (float64 1d array 301)
    ├── cutoff_diffnorm, cutoff_iteration, etc.
```

**Current Implementation**:
- ✅ Basic structure in `writeDIC2DPairResults()`
- ❌ **MISSING**: CorCoeffVec as cell array (currently single vector)
- ❌ **MISSING**: Points as cell array with proper 2D arrays (Nx2)
- ❌ **MISSING**: ncorrInfo struct with all fields
- ❌ **MISSING**: Proper dimensions (300 frames vs 150)

---

### 8. **myDIC2DpairResults_C_4_C_3.mat** (MAT v5)
**Status**: ⚠️ PARTIALLY IMPLEMENTED (same as C_1_C_2)

---

### 9. **ncorr1.mat** (HDF5 v7.3)
**Status**: ✅ IMPLEMENTED

**Expected Structure**:
```
├── current_save/ (group)
│   ├── gs (object 2d array 150x1)  # Cell array with 150 frames
│   └── name, path, roi, type
├── data_dic_save/ (group)
│   ├── dispinfo/ (group)
│   └── displacements/ (group)
└── reference_save/ (group)
    ├── gs (float64 2d array 1936x1216)
    └── name, path, roi, type
```

**Current Implementation**:
- ✅ Implemented via `writeMatchingFile()` / `writeDicNcorrFile()`
- ⚠️ **NOTE**: current_save/gs should be cell array for multiple frames

---

### 10-13. **ncorr12.mat, ncorr2.mat, ncorr3.mat, ncorr4.mat, ncorr43.mat** (HDF5 v7.3)
**Status**: ✅ IMPLEMENTED (same structure as ncorr1.mat)

---

## Summary of Missing Implementations

### Critical Missing Features:

1. **Deformation Field Writing** (DIC3DPPresults)
   - All 36 deformation measures computed but NOT written to MAT file
   - Need to write as cell arrays (1x150 frames)
   - Each cell contains array of values per face

2. **Cell Array Format**
   - Points3D, corrComb, FaceCentroids, etc. need proper cell array format
   - Currently writing as structs instead of MATLAB cell arrays

3. **HDF5 Group Structure**
   - calibration/ group with DLTparameters and DLTpath
   - distortion/ group with distortionModel and distortionPath
   - Deform/ group with all 36 fields
   - Disp/ group with DispMgn and DispVec

4. **Multi-Pair Support**
   - AllPairsResults (cell array of individual pair results)
   - DIC2Dinfo (array of 2D DIC info per pair)

5. **ncorrInfo Structure**
   - Complete DIC parameters struct in myDIC2DpairResults
   - imgcorr, stepanalysis substructures

6. **Time-Series Data**
   - CorCoeffVec as cell array (300 frames, each with N points)
   - Points as cell array (300 frames, each Nx2)
   - current_save/gs as cell array (150 frames)

---

## Implementation Priority

### HIGH PRIORITY (Core Functionality):
1. ✅ Fix deformation field writing in `write3DPPresults()`
2. ✅ Implement proper cell array format for time-series data
3. ✅ Add Disp/ group writing (DispMgn, DispVec)
4. ✅ Add FaceCentroids, FaceCorrComb, FaceIsoInd writing

### MEDIUM PRIORITY (Complete Structure):
5. ✅ Add calibration/ and distortion/ groups
6. ✅ Implement AllPairsResults and DIC2Dinfo
7. ✅ Fix myDIC2DpairResults cell array formats
8. ✅ Add ncorrInfo complete structure

### LOW PRIORITY (Enhancements):
9. ⚠️ Add RBM (Rigid Body Motion) support
10. ⚠️ Add Deform_ARBM (after RBM removal)
11. ⚠️ Optimize HDF5 compression settings

---

## Files Requiring Modification

1. **src/mat_writer.cpp** - Main implementation file
   - `write3DPPresults()` - Add all deformation fields
   - `write3DCombinedResults()` - Add calibration/distortion groups
   - `writeDIC2DPairResults()` - Fix cell array formats
   - Add helper functions for cell array creation

2. **include/mat_writer.h** - Header file
   - Add cell array helper function declarations
   - Update structure documentation

3. **include/dic_structures.h** - Data structures
   - Ensure all fields match MATLAB structure
   - Add missing calibration/distortion fields

4. **src/strain_computation.cpp** - Already computes all fields
   - ✅ No changes needed (computation is complete)

---

## Testing Requirements

Need comprehensive MAT file comparator to verify:
1. Structure hierarchy matches exactly
2. Data types match (float64, uint8, etc.)
3. Array dimensions match
4. Cell array formats are correct
5. HDF5 groups are properly nested
6. All fields are present and non-empty

