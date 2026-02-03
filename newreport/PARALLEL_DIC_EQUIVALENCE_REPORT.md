# Parallel DIC Implementation Equivalence Analysis

## Executive Summary

This report analyzes the C++ parallel DIC implementation against the MATLAB reference implementation to determine if they produce equivalent results despite using different parallelization strategies.

**Key Finding**: The implementations have **critical differences** that may lead to **non-equivalent results**, primarily in:
1. **Reference update trigger logic**
2. **Timing of ROI updates**
3. **Seed propagation during reference updates**

---

## 1. Parallelization Strategy Comparison

### MATLAB Implementation
```
Processing Mode: SEQUENTIAL (with within-frame parallelization)

Loop Structure:
  for frame = 1 to N:
    1. Seed analysis (if needed)
    2. DIC computation (ncorr_alg_rgdic - parallelized internally via OpenMP threads)
    3. Store results
    4. Update ROI
```

**Parallelization**: Across seed threads within each frame, NOT across frames

### C++ Parallel Implementation
```
Processing Mode: TWO-PHASE

Phase 1 (Sequential):
  for frame = 1 to N:
    1. Seed analysis
    2. IF seed fails:
       - Trigger reference update
       - Compute full DIC for previous frame
       - Update reference image
       - Update ROI
       - Propagate seeds
    3. Store seed data + optimizer

Phase 2 (Parallel):
  parallel for frame = 1 to N:
    - Use precomputed seed data
    - Compute DIC independently
```

**Parallelization**: Across frames (each frame runs on different thread)

---

## 2. Reference Update Trigger: CRITICAL DIFFERENCE

### MATLAB: Implicit Trigger (Unknown)

The MATLAB code does NOT show explicit reference update logic in `ncorr_alg_dicanalysis.m`. Looking at the main loop:

```matlab
[displacements_buffer,rois_dic_buffer,seedinfo_buffer,outstate_dic] = ncorr_alg_dicanalysis(
    imgs(imgcorr_prelim(end).idx_ref+1:-1:imgcorr_prelim(end).idx_cur+1), ...
```

The reference updates happen **externally** in the calling code (`ncorr.m`), based on:
- Manual decision by the algorithm
- NOT based on seed quality metrics
- Backward processing handles this differently

**Location**: Lines 1129-1244 in `ncorr.m` show the backward loop structure, but reference update decision is implicit.

### C++ Parallel: Explicit Trigger Based on Seed Analysis

```cpp
// From compute_only_seed_points() - Lines 4331-4384
SeedAnalysisResult seed_results = analyze_seeds(
    sr_nloptimizer,
    A_ref_current,
    roi_current,
    seeds_current,
    r,
    50,     // cutoff_iteration
    0.1,    // cutoff_max_diffnorm
    0.5     // cutoff_max_corrcoef
);

if (seed_results.success) {
    // Continue with current reference
    selected_data.emplace_back(roi_current, seed_results.seeds, sr_nloptimizer);
    idx++;
} else {
    // TRIGGER REFERENCE UPDATE
    // Update reference image and ROI
    A_ref_current = A_prev;
    roi_current = update(prev_roi, disps, interp_type);
    seeds_current = propagate_seeds(prev_seedparams, scalefactor);
}
```

**Trigger Criteria**: 
- Seed analysis fails (any seed has corrcoef > 0.5 OR diffnorm > 0.1)
- This is a **predictive** approach

### ⚠️ EQUIVALENCE ISSUE #1

**Problem**: The C++ implementation triggers reference updates based on **seed quality prediction**, while MATLAB's trigger mechanism is not visible in the seed analysis code. This likely leads to:

1. **Different update frequencies**: C++ may update more or less often
2. **Different update positions**: Updates may occur at different frames
3. **Cascading differences**: Once an update happens at different times, all subsequent results diverge

---

## 3. ROI Update Mechanism

### MATLAB: `update_roi()` Method

```matlab
% From ncorr_class_roi.m - Line 405
function roi_update = update_roi(obj,plot_u,plot_v,roi_plot,size_mask_update,spacing,radius)
% This function returns an updated ROI, based on the inputted
% displacement fields. This works by updating the boundary and
% redrawing the mask.
```

**Process** (from lines 405-500 in `ncorr_class_roi.m`):
1. Takes displacement fields (plot_u, plot_v)
2. Takes the DIC ROI (roi_plot) which is the union of ref ROI and valid points
3. Updates boundary by interpolating displacements
4. Redraws mask from updated boundary
5. Returns new ROI for the current image

**Called After**: DIC computation completes for each frame

```matlab
% From ncorr.m - Lines 1188-1193 (backward mode)
imgs(imgcorr_prelim(end).idx_ref-i).roi = imgs(imgcorr_prelim(end).idx_ref+1).roi.update_roi(
    displacements_buffer(i+1).plot_u,
    displacements_buffer(i+1).plot_v,
    rois_dic_buffer(i+1),
    [imgs(imgcorr_prelim(end).idx_ref-i).imginfo.height imgs(imgcorr_prelim(end).idx_ref-i).imginfo.width],
    obj.data_dic.dispinfo.spacing,
    obj.data_dic.dispinfo.radius);
```

### C++ Parallel: `update()` Function

```cpp
// From ncorr.cpp - Line 660
ROI2D update(const ROI2D &roi, const Disp2D &disp, INTERP interp_type, ROI_UPDATE_MODE mode) {
    // Update roi by updating each region boundary
    std::vector<ROI2D::region_boundary> boundaries_updated(roi.size_regions());
    for (difference_type region_idx = 0; region_idx < roi.size_regions(); ++region_idx) {
        // Get interpolator for this region
        auto disp_interp = disp.get_nlinfo_interpolator(region_idx, interp_type);
        
        if (mode == ROI_UPDATE_MODE::SKIP_ALL) {
            // Original behavior: fail entire boundary if any point returns NaN
            boundary_updated.add = details::update_boundary_skip_all(boundary.add, disp_interp, roi_scalefactor);
        } else {
            // SKIP_INVALID mode: skip individual NaN/out-of-bounds points
            boundary_updated.add = details::update_boundary_skip_invalid(boundary.add, disp_interp, roi_scalefactor);
        }
    }
    return ROI2D(std::move(boundaries_updated), roi.height(), roi.width());
}
```

**Process**:
1. Takes displacement field (Disp2D)
2. Updates boundary using displacement interpolation
3. Supports two modes:
   - `SKIP_ALL`: Fails if any boundary point is invalid (like MATLAB)
   - `SKIP_INVALID`: Skips only invalid points (more robust)

**Called During**: Reference update in Phase 1 (before parallel DIC)

```cpp
// From compute_only_seed_points() - Line 4376
roi_current = update(prev_roi, disps, interp_type);
```

### ⚠️ EQUIVALENCE ISSUE #2

**Timing Difference**:
- **MATLAB**: Updates ROI **after** DIC computation for each frame
- **C++**: Updates ROI **before** parallel DIC, during reference update

This means:
1. In MATLAB, the DIC uses the **old** ROI, then updates it for the next frame
2. In C++, the ROI is updated **during** the seeding phase when reference changes

**Potential Impact**: If ROI shapes differ, the valid point sets will differ, leading to different displacement fields.

---

## 4. Seed Propagation

### MATLAB: Seed Propagation Logic

```matlab
% From ncorr_alg_dicanalysis.m - Lines 86-93
for j = 0:size(params_init,1)-1
    % Must round displacements so that the updated position
    % lies properly on "spaced grid" - THIS IS IMPORTANT!!!
    pos_seed(j+1,1) = params_init(j+1,i+1,end).paramvector(1) + ...
                      round(params_init(j+1,i+1,end).paramvector(3)/(spacing+1))*(spacing+1);
    pos_seed(j+1,2) = params_init(j+1,i+1,end).paramvector(2) + ...
                      round(params_init(j+1,i+1,end).paramvector(4)/(spacing+1))*(spacing+1);
end
```

**Key Points**:
- Uses `paramvector(3)` and `paramvector(4)` which are `u` and `v` displacements
- Rounds to spaced grid: `round(u/(spacing+1))*(spacing+1)`
- Updates both x and y positions

### C++ Parallel: Seed Propagation Logic

```cpp
// From ncorr.cpp - Lines 4395-4410
std::vector<SeedParams> propagate_seeds(const std::vector<SeedParams>& seeds, ROI2D::difference_type spacing) {
    std::vector<SeedParams> propagated_seeds;
    propagated_seeds.reserve(seeds.size());
    
    for (const auto& seed : seeds) {
        SeedParams new_seed = seed;
        
        // Update position based on displacement, rounding to spaced grid
        // This matches the Matlab logic: pos + round(u/(spacing+1))*(spacing+1)
        new_seed.x = seed.x + std::round(seed.u / (spacing + 1)) * (spacing + 1);
        new_seed.y = seed.y + std::round(seed.v / (spacing + 1)) * (spacing + 1);
        
        propagated_seeds.push_back(new_seed);
    }
    
    return propagated_seeds;
}
```

**Key Points**:
- Uses `seed.u` and `seed.v` displacements
- Same rounding formula: `round(u/(spacing+1))*(spacing+1)`
- Updates x and y positions

### ✅ EQUIVALENCE CHECK: PASSED

The seed propagation logic is **mathematically equivalent** between MATLAB and C++.

**However**: Since reference updates happen at different times, the displacement values used for propagation will differ, leading to different seed positions.

---

## 5. Seed Analysis Quality Thresholds

### MATLAB: Unknown Thresholds

The seed analysis in MATLAB happens in `ncorr_alg_seedanalysis.m`, which is called from `ncorr_alg_dicanalysis.m`:

```matlab
% Lines 108-119
[seedinfo_buffer,convergence_buffer,outstate_seeds] = ncorr_alg_seedanalysis(
    imgs(1).imginfo,
    [imgs(2:end).imginfo],
    imgs(1).roi,
    num_region,
    pos_seed,
    radius,
    cutoff_diffnorm,
    cutoff_iteration,
    enabled_stepanalysis,
    subsettrunc,
    num_img,
    total_imgs);
```

The thresholds used are:
- `cutoff_diffnorm`: User-provided
- `cutoff_iteration`: User-provided

**Success Criteria**: Not explicitly visible in the code provided, but likely based on convergence.

### C++ Parallel: Explicit Thresholds

```cpp
// From compute_only_seed_points() - Lines 4331-4340
SeedAnalysisResult seed_results = analyze_seeds(
    sr_nloptimizer,
    A_ref_current,
    roi_current,
    seeds_current,
    r,
    50,     // cutoff_iteration
    0.1,    // cutoff_max_diffnorm
    0.5     // cutoff_max_corrcoef
);
```

**Hardcoded Thresholds**:
- `cutoff_iteration = 50`
- `cutoff_max_diffnorm = 0.1`
- `cutoff_max_corrcoef = 0.5`

**Success Criteria** (from `analyze_seeds()` - Lines 4505-4512):
```cpp
if (seed_result.corrcoef > cutoff_max_corrcoef || convergence.diffnorm > cutoff_max_diffnorm) {
    result.success = false;
}
```

### ⚠️ EQUIVALENCE ISSUE #3

**Problem**: 
1. C++ uses **hardcoded** thresholds that may not match MATLAB's behavior
2. C++ uses `corrcoef > 0.5` as a failure criterion, which may be too strict or too lenient
3. These thresholds directly control when reference updates occur

**Impact**: This is the root cause of different reference update patterns.

---

## 6. What Gets Updated During Reference Update

### MATLAB Backward Mode

When processing backward (last frame to first), MATLAB:

1. **Stores displacement fields** for each frame analyzed
2. **Updates ROIs** for each frame after DIC completes
3. **Does NOT update reference image during the batch** - instead, it uses seed propagation
4. **Reference update** happens between batches:
   ```matlab
   % Line 1136
   imgcorr_prelim(end+1).idx_ref = imgcorr_prelim(end).idx_cur;
   ```
   The previous current frame becomes the new reference frame

5. **Re-computes intermediate displacements** if buffer has multiple frames (Lines 1202-1236)

### C++ Parallel Phase 1

When detecting a reference update, C++:

1. **Computes full DIC** for the previous frame:
   ```cpp
   auto disps = compute_displacements(
       prev_sr_nloptimizer,
       prev_roi.reduce(scalefactor),
       prev_seedparams[region_idx],
       scalefactor,
       cutoff_corrcoef,
       region_idx,
       debug
   );
   ```

2. **Updates reference image**:
   ```cpp
   A_ref_current = A_prev;
   ```

3. **Updates ROI**:
   ```cpp
   roi_current = update(prev_roi, disps, interp_type);
   ```

4. **Propagates seeds**:
   ```cpp
   seeds_current = propagate_seeds(prev_seedparams, scalefactor);
   ```

### ⚠️ EQUIVALENCE ISSUE #4

**Major Difference**: 
- **MATLAB**: Reference updates happen at predetermined intervals (controlled by user via step analysis)
- **C++**: Reference updates happen **dynamically** based on seed quality prediction

This means:
1. The frame chosen as the new reference may differ
2. The displacement field used for ROI update may differ
3. The seed positions after propagation will differ

---

## 7. Displacement Computation Logic

### MATLAB: `ncorr_alg_rgdic`

The core RGDIC algorithm in MATLAB (MEX file):
- Uses priority queue based on correlation coefficient
- Processes points using IC-GN (Inverse Compositional Gauss-Newton)
- Thread-safe parallelization across seed threads
- Returns displacement fields with valid points

### C++ Parallel: `compute_displacements()`

```cpp
// From ncorr.cpp - Lines 4187-4290
Disp2D compute_displacements(
    const details::subregion_nloptimizer &sr_nloptimizer,
    const ROI2D& roi_reduced,
    const SeedParams& seedparams,
    // ...
) {
    // Uses same priority queue approach
    // Uses same IC-GN optimization
    // Single-threaded within each frame
    // Returns Disp2D with valid points
}
```

### ✅ EQUIVALENCE CHECK: LIKELY EQUIVALENT

The core RGDIC algorithm appears to be equivalent:
1. Both use priority queue (lowest corrcoef processed first)
2. Both use IC-GN optimization
3. Both use same cutoff criteria (delta_disp, corrcoef)

**However**: If different seeds or ROIs are used (due to Issues #1-#4), the results will still differ.

---

## 8. Summary of Equivalence Issues

| Issue | Component | MATLAB Behavior | C++ Parallel Behavior | Impact | Severity |
|-------|-----------|-----------------|----------------------|--------|----------|
| #1 | Reference Update Trigger | Implicit/manual | Seed quality prediction | Different update timing | **CRITICAL** |
| #2 | ROI Update Timing | After DIC | During Phase 1 (before parallel DIC) | Different ROI shapes | **HIGH** |
| #3 | Seed Quality Thresholds | User-defined | Hardcoded (corrcoef=0.5, diffnorm=0.1) | Different failure detection | **CRITICAL** |
| #4 | Reference Frame Selection | Predetermined | Dynamic | Different reference images | **CRITICAL** |
| #5 | Processing Direction | Backward (last→first) | Forward (first→last) | May affect accumulation | **MEDIUM** |

---

## 9. Will the Results be Equivalent?

### Answer: **NO - Results will NOT be equivalent**

**Primary Reasons**:

1. **Reference updates happen at different frames**: The seed quality thresholds in C++ (corrcoef > 0.5, diffnorm > 0.1) are arbitrary and unlikely to match MATLAB's implicit update logic.

2. **Cascading divergence**: Once a single reference update happens at a different frame, ALL subsequent results diverge because:
   - Different reference images are used
   - Different ROIs are used
   - Different seed positions are used
   - Different displacement fields are computed

3. **Lack of validation data**: The C++ thresholds are not tuned to match MATLAB behavior.

---

## 10. Recommendations to Achieve Equivalence

### Option A: Match MATLAB's Reference Update Logic (Recommended)

1. **Remove predictive reference updates** from C++ implementation
2. **Use explicit frame intervals** for reference updates (matching MATLAB's step analysis)
3. **Make thresholds configurable** via DIC_analysis_parallel_input

### Option B: Make C++ Thresholds Configurable

1. Add parameters to `DIC_analysis_parallel_input`:
   ```cpp
   struct DIC_analysis_parallel_input {
       // ...
       double cutoff_max_diffnorm;    // Already exists
       double cutoff_max_corrcoef;    // Already exists
       bool enable_auto_ref_update;   // NEW: Enable/disable prediction
       int ref_update_interval;       // NEW: Fixed interval (0 = auto)
   };
   ```

2. Modify `compute_only_seed_points()` to support fixed intervals:
   ```cpp
   if (enable_auto_ref_update) {
       // Use seed quality prediction
       if (!seed_results.success) { update_reference(); }
   } else if (ref_update_interval > 0) {
       // Use fixed intervals (like MATLAB)
       if (idx % ref_update_interval == 0) { update_reference(); }
   }
   ```

### Option C: Hybrid Approach

1. **Phase 1**: Use MATLAB to generate seed data and reference update schedule
2. **Phase 2**: Use C++ parallel implementation with pre-determined updates
3. This maintains parallelization benefits while ensuring equivalence

---

## 11. Testing Strategy for Equivalence

To verify equivalence, you need:

1. **Disable automatic reference updates** in C++
2. **Use same reference frames** as MATLAB
3. **Compare displacement fields** frame-by-frame:
   ```python
   for frame in frames:
       diff_u = matlab_disp[frame].u - cpp_disp[frame].u
       diff_v = matlab_disp[frame].v - cpp_disp[frame].v
       max_error = max(abs(diff_u).max(), abs(diff_v).max())
       print(f"Frame {frame}: max_error = {max_error}")
   ```

4. **Expected tolerance**: < 1e-6 pixels if truly equivalent

---

## 12. Current Implementation Assessment

### Strengths
- ✅ Achieves true frame-level parallelization
- ✅ Core RGDIC algorithm appears correct
- ✅ Seed propagation logic matches MATLAB
- ✅ ROI update mechanism is equivalent (when used)

### Weaknesses
- ❌ Reference update trigger logic differs fundamentally
- ❌ Hardcoded thresholds not validated against MATLAB
- ❌ No mechanism to force specific reference update frames
- ❌ Cannot guarantee equivalent results to MATLAB

### Risk Assessment
**HIGH RISK** of producing different results than MATLAB due to reference update timing differences. This is acceptable if:
- The C++ implementation is validated independently on test cases
- The predictive approach is shown to work well empirically
- Users understand this is an enhancement, not a port

**NOT ACCEPTABLE** if the goal is to exactly reproduce MATLAB results for validation purposes.

---

## 13. Critical Implementation Bug Found

### C++ `analyze_seeds()` - Line 4510-4511

```cpp
if (seed_result.corrcoef > cutoff_max_corrcoef || convergence.diffnorm > cutoff_max_diffnorm) {
    std::cout << result.success << std::endl;
    result.success = false; //TODO: Solve this
}
```

**CRITICAL ISSUE**: There's a TODO comment indicating this logic may not be correct!

The condition sets `result.success = false` if **ANY** seed fails the quality check. This means:
- If you have multiple seeds and **even one** fails, the entire frame triggers a reference update
- This is extremely sensitive and will likely cause reference updates at every frame

**Expected Behavior**: In MATLAB with multiple seed threads, if one thread fails, the others can still succeed. The C++ implementation should probably:
1. Track success per seed
2. Only trigger reference update if **ALL** seeds fail, OR
3. Use a percentage threshold (e.g., if >50% of seeds fail)

---

## 14. Additional Critical Finding: Reference Update on First Failure

### Problem with Current Logic

Looking at `compute_only_seed_points()` line 4350-4384:

```cpp
if (seed_results.success) {
    // Continue with current reference
    selected_data.emplace_back(roi_current, seed_results.seeds, sr_nloptimizer);
    idx++;
} else {
    // TRIGGER REFERENCE UPDATE
    auto A_prev = A_curs[idx-1];  // <-- This will CRASH if idx == 0!
```

**BUG**: If the very first frame fails seed analysis (idx=0), this will access `A_curs[-1]` which is out of bounds!

**Implications**:
1. The implementation assumes at least one frame succeeds before any failure
2. There's no handling for the case where the initial reference is bad
3. This is a potential segmentation fault

---

## Conclusion

The C++ parallel DIC implementation is **NOT equivalent** to the MATLAB implementation due to:

1. ❌ **Different reference update logic** - seed quality prediction vs. predetermined intervals
2. ❌ **Bug in seed failure detection** - sets failure if ANY seed fails (line 4511)
3. ❌ **Hardcoded thresholds** - corrcoef > 0.5, diffnorm > 0.1 (not validated)
4. ❌ **Potential crash bug** - doesn't handle first frame failure (line 4356)
5. ❌ **Different processing direction** - forward vs. backward

**To achieve equivalence**, you must:
1. **Fix the seed analysis bug** (line 4511) - define proper failure criteria
2. **Fix the crash bug** (line 4356) - handle idx == 0 case
3. **Remove dynamic reference updates** - use fixed intervals like MATLAB
4. **Make thresholds configurable** - match MATLAB's user-defined values
5. **Add validation mode** - option to force specific reference update frames

The parallelization strategy itself is sound, but **the implementation has critical bugs** that need fixing before it can be considered equivalent or even safe to use.
