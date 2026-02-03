# AllPairsResults & DIC2Dinfo - Fix Plan

## Problem Summary

**Current State**: Both `AllPairsResults` and `DIC2Dinfo` are written as **empty placeholders** ❌

**Root Cause**: The C++ implementation doesn't properly store or write individual pair results before stitching.

---

## MATLAB vs C++ Comparison

### MATLAB Workflow (step3_dic_rewrited.m):
```matlab
% 1. Process each stereo pair individually
for ip=1:nPairs
    DIC3DpairResults = struct;  % Individual pair result
    % ... populate with 3D reconstruction ...
    DIC3DAllPairsResults{ip} = DIC3DpairResults;  % Store individual
end

% 2. Stitch pairs together
DIC3Dcombined = DIC3DsurfaceStitch(DIC3DAllPairsResults, pairIndList);

% 3. Add individual results to combined structure
DIC3Dcombined.AllPairsResults = DIC3DAllPairsResults;  % Keep individuals!
DIC3Dcombined.DIC2Dinfo{ipair} = DIC2D{ipair};  % Keep 2D data!
```

### C++ Current Implementation:
```cpp
// ❌ PROBLEM: Individual pair results are NOT saved!
// Only the stitched result exists

DIC3Dcombined stitchPairsSimple(const std::vector<DIC3Dcombined>& all_pairs) {
    // This receives already-combined pairs, not individual results!
}
```

---

## Required Data Structures

### 1. DIC3DpairResults (NEW - needs to be created)

```cpp
struct DIC3DpairResults {
    // Camera pair info
    std::vector<int> cameraPairInd;  // [cam1, cam2]
    
    // Calibration
    CalibrationData calibration;  // DLTpath, DLTparameters (2x1 cells)
    
    // Distortion
    DistortionData distortion;  // distortionModel, distortionPath (2x1 cells)
    
    // Geometry
    std::vector<int> Faces;  // 3 x nFaces (triangle connectivity)
    std::vector<double> FaceColors;  // 1 x nFaces
    
    // Per-frame data (cell arrays nImages x 1)
    std::vector<Points3D> Points3D;  // 3D points per frame
    DispData Disp;  // DispVec, DispMgn per frame
    std::vector<Points3D> FaceCentroids;  // Face centroids per frame
    std::vector<std::vector<double>> corrComb;  // Combined correlation per frame
    std::vector<std::vector<double>> FaceCorrComb;  // Face correlation per frame
};
```

### 2. DIC3Dcombined (UPDATE - add fields)

```cpp
struct DIC3Dcombined {
    // ... existing fields (Points3D, Faces, etc.) ...
    
    // ADD THESE:
    std::vector<DIC3DpairResults> AllPairsResults;  // Individual pair results
    std::vector<DIC2DPairResults> DIC2Dinfo;  // 2D DIC info per pair
};
```

---

## Implementation Plan

### Phase 1: Create Structures ✅

1. Add `DIC3DpairResults` to `include/dic_structures.h`
2. Add `AllPairsResults` and `DIC2Dinfo` fields to `DIC3Dcombined`
3. Add binary serialization for new fields

### Phase 2: Modify Reconstruction Workflow

**File**: `src/dic_analysis.cpp` (or wherever 3D reconstruction happens)

**Changes**:
1. Create `DIC3DpairResults` for each stereo pair **before** stitching
2. Store individual results in `DIC3Dcombined.AllPairsResults`
3. Store 2D DIC data in `DIC3Dcombined.DIC2Dinfo`

**Example**:
```cpp
// For each stereo pair
for (int ip = 0; ip < num_pairs; ip++) {
    DIC3DpairResults pair_result;
    
    // Populate pair_result with 3D reconstruction
    pair_result.cameraPairInd = {cam1, cam2};
    pair_result.calibration = ...;
    pair_result.Points3D = ...;  // Per frame
    // ... etc ...
    
    // Store individual result
    all_pair_results.push_back(pair_result);
    
    // Load corresponding 2D DIC data
    DIC2DPairResults dic2d = loadDIC2DPairResults(...);
    dic2d_info.push_back(dic2d);
}

// Stitch pairs
DIC3Dcombined stitched = stitchPairs(all_pair_results);

// Add individual results to stitched
stitched.AllPairsResults = all_pair_results;
stitched.DIC2Dinfo = dic2d_info;
```

### Phase 3: Implement MAT File Writing

**File**: `src/mat_writer.cpp`

**Update `writeAllPairsResults()`**:
```cpp
bool MatWriter::writeAllPairsResults(mat_t* matfp,
                                    const std::vector<DIC3DpairResults>& all_pairs) {
    // Create cell array (1 x nPairs)
    matvar_t* cell_array = Mat_VarCreate("AllPairsResults", ...);
    
    for (size_t i = 0; i < all_pairs.size(); ++i) {
        // Create struct for this pair
        matvar_t* pair_struct = createDIC3DpairStruct(all_pairs[i]);
        Mat_VarSetCell(cell_array, i, pair_struct);
    }
    
    Mat_VarWrite(matfp, cell_array, MAT_COMPRESSION_NONE);
    return true;
}
```

**Update `writeDIC2Dinfo()`**:
```cpp
bool MatWriter::writeDIC2Dinfo(mat_t* matfp,
                              const std::vector<DIC2DPairResults>& dic2d_info) {
    // Create cell array (nPairs x 1)
    matvar_t* cell_array = Mat_VarCreate("DIC2Dinfo", ...);
    
    for (size_t i = 0; i < dic2d_info.size(); ++i) {
        // Reuse existing writeDIC2DPairResults logic
        matvar_t* dic2d_struct = createDIC2DPairStruct(dic2d_info[i]);
        Mat_VarSetCell(cell_array, i, dic2d_struct);
    }
    
    Mat_VarWrite(matfp, cell_array, MAT_COMPRESSION_NONE);
    return true;
}
```

---

## Questions for User

Before implementing, please confirm:

1. **Where does 3D reconstruction happen** in the C++ code?
   - Is it in `dic_analysis.cpp`?
   - Or in a separate reconstruction file?

2. **Do we have access to individual pair data** before stitching?
   - Or do we need to modify the workflow to save it?

3. **Should we load DIC2DPairResults** from the MAT files created in Step 2?
   - Files like `myDIC2DpairResults_C_1_C_2.mat`

4. **Priority**: Is this blocking other work?
   - Should we implement this now or defer?

---

## Estimated Effort

- **Phase 1** (Structures): 1-2 hours
- **Phase 2** (Workflow): 3-4 hours (depends on current code structure)
- **Phase 3** (Writing): 2-3 hours
- **Testing**: 2 hours

**Total**: ~8-11 hours

---

## Alternative: Quick Fix (Not Recommended)

If we need a quick workaround:
1. Write `AllPairsResults` as empty (current state)
2. Write `DIC2Dinfo` by **loading** the myDIC2DPairResults MAT files
3. Document as "partial implementation"

This would allow MATLAB to load the file, but `AllPairsResults` would be empty.

---

## Recommendation

**Implement the full solution** (Phases 1-3) to ensure complete MATLAB compatibility.

The current placeholder approach will cause issues when MATLAB tries to access individual pair data for visualization or post-processing.

---

**Next Step**: Please review and confirm approach before I proceed with implementation.

