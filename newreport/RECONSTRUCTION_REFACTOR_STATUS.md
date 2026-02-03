# 3D Reconstruction Refactoring - In Progress

## Objective
Properly implement `AllPairsResults` and `DIC2Dinfo` by refactoring the 3D reconstruction workflow to create individual `DIC3DpairResults` structures before stitching.

## Changes Completed ✅

### 1. Data Structures (`include/dic_structures.h`)
- ✅ Created `DIC3DpairResults` struct for individual stereo pair results
- ✅ Updated `DIC3Dcombined.AllPairsResults` to use `std::vector<DIC3DpairResults>`
- ✅ `DIC3Dcombined.DIC2Dinfo` already exists as `std::vector<DIC2DPairResults>`

### 2. Surface Stitching (`src/surface_stitching.cpp`, `include/surface_stitching.h`)
- ✅ Updated `stitchPairsSimple()` to accept `std::vector<DIC3DpairResults>`
- ✅ Updated `stitchPairsGeometric()` to accept `std::vector<DIC3DpairResults>`
- ✅ Fixed calibration/distortion data merging to use individual pair fields

## Changes In Progress ⏳

### 3. 3D Reconstruction Workflow (`src/dic_analysis.cpp`)

**Current Issue**: Line 763 creates `DIC3Dcombined combined;` but should create `DIC3DpairResults pair_result;`

**Required Changes**:
1. Change loop variable from `DIC3Dcombined` to `DIC3DpairResults`
2. Populate `DIC3DpairResults` fields instead of `DIC3Dcombined` fields
3. After stitching, populate `stitched.AllPairsResults` with individual pairs
4. Load `DIC2DPairResults` from Step 2 output files
5. Populate `stitched.DIC2Dinfo` with loaded 2D data

**Code Location**: `src/dic_analysis.cpp:612-944`

## Next Steps

1. **Refactor reconstruction loop** (lines 620-934):
   - Change `std::vector<DIC3Dcombined> all_pairs` to `std::vector<DIC3DpairResults> all_pairs`
   - Update field assignments to match `DIC3DpairResults` structure
   
2. **Load DIC2D data** after reconstruction:
   - Load `myDIC2DpairResults_C_X_C_Y.mat` files for each pair
   - Store in `stitched.DIC2Dinfo`

3. **Store individual pairs**:
   - After stitching: `stitched.AllPairsResults = all_pairs;`
   - After loading 2D: `stitched.DIC2Dinfo = dic2d_results;`

4. **Update MAT writer** (`src/mat_writer.cpp`):
   - Implement `writeAllPairsResults()` to write cell array of `DIC3DpairResults`
   - Implement `writeDIC2Dinfo()` to write cell array of `DIC2DPairResults`

## Files Modified So Far

- `include/dic_structures.h` ✅
- `include/surface_stitching.h` ✅
- `src/surface_stitching.cpp` ✅
- `src/dic_analysis.cpp` ⏳ (in progress)

## Files To Modify Next

- `src/dic_analysis.cpp` - Complete refactoring
- `src/mat_writer.cpp` - Implement proper writing
- `include/mat_writer.h` - Update signatures if needed

## Estimated Remaining Work

- Refactor reconstruction loop: 30 minutes
- Load DIC2D data: 20 minutes
- Implement MAT writing: 1 hour
- Testing: 30 minutes

**Total**: ~2.5 hours

