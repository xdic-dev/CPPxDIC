# AllPairsResults and DIC2Dinfo Implementation Analysis

## Current Status: PLACEHOLDER ONLY ❌

Both `AllPairsResults` and `DIC2Dinfo` are currently written as **empty cell arrays** (placeholders).

---

## MATLAB Structure Analysis

### From `step3_dic_rewrited.m` (lines 104-271):

```matlab
DIC3DAllPairsResults=cell(nPairs,1);  % Cell array for all pairs

for ip=1:nPairs
    DIC3DpairResults=struct;  % Individual pair result
    
    % Populate DIC3DpairResults with:
    DIC3DpairResults.cameraPairInd = [nCamRef nCamDef];
    DIC3DpairResults.calibration.DLTpath{1} = ...
    DIC3DpairResults.calibration.DLTpath{2} = ...
    DIC3DpairResults.calibration.DLTparameters{1} = L1;
    DIC3DpairResults.calibration.DLTparameters{2} = L2;
    DIC3DpairResults.Faces = F;
    DIC3DpairResults.FaceColors = FC;
    DIC3DpairResults.distortionModel{1} = ...
    DIC3DpairResults.distortionModel{2} = ...
    DIC3DpairResults.distortionPath{1} = ...
    DIC3DpairResults.distortionPath{2} = ...
    DIC3DpairResults.Points3D = cell(nImages,1);  % Cell array per frame
    DIC3DpairResults.Disp.DispVec = cell(nImages,1);
    DIC3DpairResults.Disp.DispMgn = cell(nImages,1);
    DIC3DpairResults.FaceCentroids = cell(nImages,1);
    DIC3DpairResults.corrComb = cell(nImages,1);
    DIC3DpairResults.FaceCorrComb = cell(nImages,1);
    
    % Store in cell array
    DIC3DAllPairsResults{ip} = DIC3DpairResults;
end

% Add to combined structure
DIC3Dcombined.AllPairsResults = DIC3DAllPairsResults;  % Cell array (1x2 for 2 pairs)
DIC3Dcombined.DIC2Dinfo{ipair} = DIC2D{ipair};  % Cell array (2x1 for 2 pairs)
```

---

## File Structure from `mat_trees.txt`

### DIC3Dcombined_2Pairs_stitched.mat:
```
└── DIC3Dcombined (group)
    ├── AllPairsResults (object 2d array 1x2)  ← Cell array with 2 DIC3DpairResults
    ├── DIC2Dinfo (object 2d array 2x1)        ← Cell array with 2 DIC2DPairResults
    ...
```

---

## Required Implementation

### 1. **AllPairsResults** (Cell Array 1 x nPairs)

Each cell contains a **DIC3DpairResults** struct with:

**Fields**:
- `cameraPairInd` (1x2 array): [cam1, cam2]
- `calibration` (struct):
  - `DLTpath` (cell 1x2): paths to DLT files
  - `DLTparameters` (cell 1x2): DLT parameter vectors
- `distortionModel` (cell 1x2): distortion model names
- `distortionPath` (cell 1x2): paths to distortion files
- `Faces` (3 x nFaces array): triangle connectivity
- `FaceColors` (1 x nFaces array): face colors
- `Points3D` (cell nImages x 1): 3D points per frame
- `Disp` (struct):
  - `DispVec` (cell nImages x 1): displacement vectors per frame
  - `DispMgn` (cell nImages x 1): displacement magnitudes per frame
- `FaceCentroids` (cell nImages x 1): face centroids per frame
- `corrComb` (cell nImages x 1): combined correlation per frame
- `FaceCorrComb` (cell nImages x 1): face correlation per frame

### 2. **DIC2Dinfo** (Cell Array nPairs x 1)

Each cell contains a **DIC2DPairResults** struct with all fields from Phase 3:
- `nCamRef`, `nCamDef`, `nImages`
- `ROImask`
- `ncorrInfo` (full struct with stepanalysis)
- `Points` (cell array 301 x 1)
- `CorCoeffVec` (cell array 301 x 1)
- `Faces`, `FaceColors`

---

## Current C++ Implementation Issues

### Issue 1: Wrong Input Type for AllPairsResults

**Current**:
```cpp
bool MatWriter::writeAllPairsResults(mat_t* matfp,
                                    const std::vector<DIC3Dcombined>& all_pairs)
```

**Problem**: Receives `DIC3Dcombined` (stitched result), but needs **individual pair results**!

**Solution**: Need a new structure `DIC3DpairResults` or use existing data differently.

### Issue 2: Empty Placeholders

**Current**:
```cpp
// For now, create empty cells as placeholders
for (size_t i = 0; i < n_entries; ++i) {
    Mat_VarSetCell(obj_array, i, nullptr);  // Placeholder
}
```

**Problem**: No actual data written!

---

## Implementation Strategy

### Option A: Create DIC3DpairResults Structure (RECOMMENDED)

1. **Add new struct** to `dic_structures.h`:
```cpp
struct DIC3DpairResults {
    std::vector<int> cameraPairInd;  // [cam1, cam2]
    CalibrationData calibration;
    DistortionData distortion;
    std::vector<int> Faces;
    std::vector<double> FaceColors;
    std::vector<Points3D> Points3D;  // Per frame
    DispData Disp;
    std::vector<Points3D> FaceCentroids;
    std::vector<std::vector<double>> corrComb;
    std::vector<std::vector<double>> FaceCorrComb;
};
```

2. **Update DIC3Dcombined** to store individual pairs:
```cpp
struct DIC3Dcombined {
    // ... existing fields ...
    std::vector<DIC3DpairResults> AllPairsResults;  // Individual pair results
    std::vector<DIC2DPairResults> DIC2Dinfo;  // 2D DIC info per pair
};
```

3. **Implement full writing** in `writeAllPairsResults()` and `writeDIC2Dinfo()`.

### Option B: Extract from Existing Data

If individual pair data is not stored, we need to:
1. Modify the reconstruction workflow to **save individual pair results** before stitching
2. Load them when writing the combined file

---

## Data Flow

### Current Workflow:
```
Step 2 (formatOutput):
  → Creates DIC2DPairResults for each pair
  → Saves to myDIC2DpairResults_C_1_C_2.mat

Step 3 (3D reconstruction):
  → Loads DIC2DPairResults
  → Creates DIC3DpairResults for each pair
  → Stitches into DIC3Dcombined
  → Saves DIC3Dcombined_2Pairs_stitched.mat
    ├── AllPairsResults ← Should contain individual DIC3DpairResults
    └── DIC2Dinfo ← Should contain DIC2DPairResults
```

### Problem:
Currently, individual `DIC3DpairResults` are **not saved** before stitching!

---

## Recommended Action Plan

1. ✅ **Verify data availability**: Check if individual pair results exist in C++ workflow
2. **Create DIC3DpairResults structure** if needed
3. **Modify reconstruction workflow** to save individual pairs
4. **Implement full writing** for AllPairsResults and DIC2Dinfo
5. **Test with MATLAB** to verify structure matches

---

## Questions to Resolve

1. **Where are individual pair results stored** in the C++ workflow?
2. **Should we create DIC3DpairResults** or reuse existing structures?
3. **Do we need to modify the reconstruction workflow** to save individual pairs?

---

## Next Steps

1. Search C++ codebase for where 3D reconstruction happens
2. Identify if individual pair data is available
3. Design structure to hold individual pair results
4. Implement full writing functions

---

**Status**: Analysis complete, awaiting decision on implementation approach.

