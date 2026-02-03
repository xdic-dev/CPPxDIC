# DIC Displacement Accumulation Analysis Report

## 1. MATLAB Code Structure and Workflow

### Overview
MATLAB's ncorr performs DIC analysis in two distinct phases:
1. **DIC Analysis Phase**: Computes step displacements (backward analysis from last to first frame)
2. **Format/Add Phase**: Post-processes and accumulates displacements

### Phase 1: DIC Analysis (Lines 1100-1244)

#### Backward Processing Mode (`type == 'backward'`)
```
Loop: while more frames to analyze
  1. Set ref_idx = last unprocessed frame (or previous idx_cur)
  2. Set idx_cur based on stepanalysis settings
  3. Call ncorr_alg_dicanalysis(imgs[ref_idx:-1:idx_cur])
     - Analyzes ALL frames from ref back to cur in ONE call
     - Returns: displacements_buffer, rois_dic_buffer, seedinfo_buffer
  4. Update ROIs for all analyzed frames
  5. IF more than 2 images analyzed (i.e., buffer has multiple displacements):
     FOR each intermediate frame (not the last one):
       - RE-COMPUTE displacement using UPDATED roi
       - This is the "update displacement fields" step (lines 1202-1236)
  6. Store step displacements and their ROIs
  7. Update imgcorr_prelim structure (tracks ref/cur indices)
```

**Key Points**:
- MATLAB analyzes BACKWARD (from last frame to first)
- Reference updates are NOT pre-determined - they happen dynamically based on correlation during analysis
- Backward processing benefits:
  - Seeds propagate from end to beginning (useful if deformation accumulates)
  - Can batch-process multiple frames in one DIC call
  - User sees results in reverse chronological order
- Each DIC call produces STEP displacements (frame i to frame i-1)
- When buffer has multiple frames, intermediate displacements are RE-COMPUTED with updated ROIs
- NO accumulation happens during DIC analysis
- Stores: `displacements_prelim[]` and `rois_dic_prelim[]` - these are STEP displacements
- **C++ uses forward processing (0→1→2→...)** which is more standard for displacement tracking

### Phase 2: Format/Add Phase (Lines 1327-1384)

#### Format Step Displacements (Line 1328)
```matlab
[plots_disp_f_step_prelim, rois_f_step_prelim, ...] = ncorr_gui_formatdisp(
    imgs_ref, imgs_cur, 
    [obj.data_dic.displacements.roi_dic],
    {obj.data_dic.displacements.plot_u_dic},
    {obj.data_dic.displacements.plot_v_dic},
    ...)
```
- Formats each step displacement independently
- Applies smoothing, extrapolation, converts to B-spline coefficients
- Each step retains its own ROI

#### Add Plots (Lines 1359-1384)
```matlab
FOR each imgcorr section (each reference update):
  FOR each frame in that section:
    idx_dispadd = [previous section ends, current frame]
    [plot_added] = ncorr_alg_addanalysis(
        {plots_step[idx_dispadd].u_formatted},
        {plots_step[idx_dispadd].v_formatted},
        rois_step[idx_dispadd],
        ...)
```

**Critical**: `ncorr_alg_addanalysis` receives:
- Array of step displacement plots
- Array of corresponding ROIs (one per step)
- Calls `ncorr_alg_adddisp` (MEX function) which uses EACH displacement's OWN ROI

### MATLAB Accumulation Strategy
1. Store ALL step displacements with their ROIs
2. For frame N, identify which steps are needed (chain from 0 to N)
3. Add them together using `add_with_rois` logic (each step uses its own ROI)

---

## 2. Accumulation Examples: C++ ON_THE_FLY vs MATLAB POST_PROCESS

### Setup
- **5 frames**: F0, F1, F2, F3, F4 (frame indices: 0, 1, 2, 3, 4)
- **Initial ROI**: R0 (1000 points)
- **Reference update threshold**: Met after analyzing frame 2

### Example: C++ ON_THE_FLY Mode

```
Initialization:
  - ref_idx = 0 (frame F0)
  - roi_ref = R0

Iteration 1 (cur_idx = 1):
  - Compute: RGDIC(F0, F1) → disp_01
  - disp_01.roi = R0 (DIC computed on R0)
  - Store: output.disps[0] = disp_01
  - Correlation check: NO UPDATE
  - ref_idx = 0 (unchanged)

Iteration 2 (cur_idx = 2):
  - Compute: RGDIC(F0, F2) → disp_02_step
  - disp_02_step.roi = R0
  - Since ref_idx == 0: Store directly
  - output.disps[1] = disp_02_step
  - Correlation check: TRIGGERS UPDATE ✓
  - ref_idx = cur_idx = 2 (now referencing frame F2)
  - roi_ref = update(R0, disp_02_step) = R2 (900 points, shape changed!)

Iteration 3 (cur_idx = 3):
  - Compute: RGDIC(F2, F3) → disp_23_step
  - disp_23_step.roi = R2 (NEW ROI!)
  - Since ref_idx = 2 > 0: Must accumulate
  - ACCUMULATE: add([output.disps[ref_idx-1], disp_23_step])
    = add([output.disps[1], disp_23_step])
    * output.disps[1] = disp_02_step with ROI = R0 (OLD roi, 1000 points)
    * disp_23_step with ROI = R2 (NEW roi, 900 points, DIFFERENT SHAPE)
    * add() uses FIRST displacement's ROI (R0) as reference
    * Problem: Many points in R0 don't exist in R2
    * Result: added_disps.roi = intersection → VERY SMALL or EMPTY!
  - Store: output.disps[2] = added_disps (degraded!)

Iteration 4 (cur_idx = 4):
  - Compute: RGDIC(F2, F4) → disp_24_step
  - disp_24_step.roi = R2
  - ACCUMULATE: add([output.disps[ref_idx-1], disp_24_step])
    = add([output.disps[1], disp_24_step])
    * Same problem as before
  - Result: EMPTY ROI → CASCADE FAILURE
```

**Key Issue**: The reference index `ref_idx = cur_idx` (the frame we just analyzed to), so when accumulating, we fetch `disps[ref_idx-1]` which contains the displacement TO that reference frame. The ROI mismatch occurs because that displacement was computed with the OLD ROI before the update.

### Example: MATLAB POST_PROCESS Mode

```
=== PHASE 1: Store Step Displacements ===

Initialization:
  - ref_idx = 0 (frame F0)
  - roi_ref = R0

Iteration 1 (cur_idx = 1):
  - Compute: RGDIC(F0, F1) → disp_01_step
  - step_disps[0] = disp_01_step
  - step_rois[0] = R0 (ROI used during computation)
  - step_ref_idx[0] = 0
  - Correlation check: NO UPDATE
  - NO accumulation yet (deferred to post-processing)

Iteration 2 (cur_idx = 2):
  - Compute: RGDIC(F0, F2) → disp_02_step
  - step_disps[1] = disp_02_step
  - step_rois[1] = R0
  - step_ref_idx[1] = 0
  - Correlation check: TRIGGERS UPDATE ✓
  - ref_idx = cur_idx = 2 (now referencing frame F2)
  - roi_ref = update(R0, disp_02_step) = R2 (900 points, shape changed)
  - NO accumulation yet

Iteration 3 (cur_idx = 3):
  - Compute: RGDIC(F2, F3) → disp_23_step
  - step_disps[2] = disp_23_step
  - step_rois[2] = R2 (NEW ROI!)
  - step_ref_idx[2] = 2
  - NO accumulation yet

Iteration 4 (cur_idx = 4):
  - Compute: RGDIC(F2, F4) → disp_24_step
  - step_disps[3] = disp_24_step
  - step_rois[3] = R2
  - step_ref_idx[3] = 2
  - NO accumulation yet

=== PHASE 2: Post-Process Accumulation ===

For Frame F1 (output.disps[0]):
  - Build chain: idx=0, step_ref_idx[0]=0 → STOP (at frame 0)
  - chain = [step_disps[0]]
  - Single step, no need to add
  - output.disps[0] = step_disps[0]

For Frame F2 (output.disps[1]):
  - Build chain: idx=1, step_ref_idx[1]=0 → STOP (at frame 0)
  - chain = [step_disps[1]]
  - Single step
  - output.disps[1] = step_disps[1]

For Frame F3 (output.disps[2]):
  - Build chain: 
    - idx=2, step_ref_idx[2]=2 (need disp TO frame 2)
    - Go to idx=1, step_ref_idx[1]=0 → STOP (at frame 0)
  - chain = [step_disps[1], step_disps[2]]
  - chain_rois = [R0, R2]
  - Call: add_with_rois([disp_02_step, disp_23_step], [R0, R2])
    * For each point p in R0:
      - Displace p using disp_02_step → p' (check bounds against R0 ✓)
      - Displace p' using disp_23_step → p'' (check bounds against R2 ✓ - uses correct ROI!)
      - If both valid, accumulate
  - output.disps[2] = accumulated result (ROBUST!)

For Frame F4 (output.disps[3]):
  - Build chain: idx=3, step_ref_idx[3]=2 → idx=1, step_ref_idx[1]=0 → STOP
  - chain = [step_disps[1], step_disps[3]]
  - chain_rois = [R0, R2]
  - Call: add_with_rois([disp_02_step, disp_24_step], [R0, R2])
  - output.disps[3] = accumulated result (ROBUST!)
```

**Solution**: Each step uses its matching ROI for bounds checking, avoiding shape mismatch issues.

---

## 3. POST_PROCESS Implementation: Problem & Solution

### The Core Problem

#### Original C++ `add()` Function Logic
```cpp
Disp2D add(const std::vector<Disp2D>& disps, INTERP interp_type) {
    // Uses FIRST displacement's ROI as reference for ALL
    const ROI2D& roi_first = disps.front().get_roi();
    
    for each point p in roi_first:
        for i=0 to disps.size()-1:
            // Check bounds using disps[i].get_roi()
            // BUT disps[i] might already be accumulated!
            // Its ROI is intersection of previous ROIs
            if (!near_nlinfo(p_current, disps[i])):
                skip point
        accumulate displacement
```

**Issue**: When `disps[i]` is an already-accumulated displacement, its ROI is the intersection of all previous ROIs, not the original step ROI.

#### Concrete Failure Scenario
```
Frame 2 update changes ROI shape:
- R0: rectangular, 1000 points
- R2: after update, irregular shape, 900 points

When accumulating for Frame 3:
- output.disps[1] is accumulated F0→F2, has ROI = R0
- disp_23_step is step F2→F3, has ROI = R2

add([output.disps[1], disp_23_step]):
  Uses output.disps[1].roi (R0) as master
  For points at R0 boundary:
    - May not exist in R2 (shape changed!)
    - Bounds check fails
    - Point excluded
  Result: Large point loss or empty ROI
```

### The Solution: `add_with_rois()`

```cpp
Disp2D add_with_rois(
    const std::vector<Disp2D>& disps,
    const std::vector<ROI2D>& rois,  // One ROI per displacement!
    INTERP interp_type)
```

**Key Difference**:
```cpp
for each point p in rois[0]:
    for i=0 to disps.size()-1:
        // Use rois[i] for bounds checking, not disps[i].get_roi()
        if (!near_nlinfo_with_roi(p_current, disps[i], rois[i])):
            skip point
```

### Why POST_PROCESS Was Needed

1. **Store Original Context**: Each step displacement needs its original ROI preserved
2. **Deferred Accumulation**: Accumulate only after all steps computed
3. **Proper Bounds Checking**: Use each step's original ROI for its bounds check
4. **Handle Shape Changes**: ROI shape changes don't cascade into previous accumulated results

### Empty ROI Cascade Explained

```
Without POST_PROCESS:
Step 1: disp[0].roi = 1000 points
Step 2: disp[1].roi = 1000 points → UPDATE → roi_ref changes shape
Step 3: add([disp[1], step_3]) → disp[2].roi = 200 points (80% loss!)
Step 4: add([disp[2], step_4]) → disp[3].roi = 0 points (EMPTY!)
  ↑ Because disp[2] already degraded

With POST_PROCESS:
Step 1-4: Store all steps with original ROIs
Post-process: Each add_with_rois uses original ROIs
Result: Minimal point loss, stable accumulation
```

---

## 4. Observations on Current Issues

### Issue 1: Too Many Reference Updates
**Possible Causes**:
- `update_corrcoef` threshold too low
- ROI update creating artificially "good" correlations
- Need to check if correlation calculation considers new ROI size

**Investigation Needed**:
- Compare `selected_corrcoef` values between C++ and MATLAB
- Check if MATLAB has additional logic preventing frequent updates

### Issue 2: Seed Point Updates
**Current Behavior**: Seeds not updated during reference changes
**MATLAB Behavior**: Lines 1205-1207 show seed propagation
```matlab
if (obj.data_dic.dispinfo.stepanalysis.auto)
    params_init = seedinfo_buffer(:,:,end-i);
end
```

**Required**: When ref_idx updates, seeds should be transformed/propagated to new reference frame.

### Issue 3: Displacement Reset on Reference Update
**Likely Cause**: In POST_PROCESS mode, step displacements are stored, but the visualization might be showing step displacements instead of accumulated ones during the loop.

**Fix**: Either:
- Keep temporary accumulated version for display
- Or accept that display shows steps until post-processing completes

---

## 5. Recommendations

1. **Manual Override Parameters**: Allow setting `roi_update_mode` and `accumulation_mode` explicitly for debugging
2. **Save Step Data**: Store `step_disps`, `step_rois`, `step_ref_idx` for analysis
3. **Correlation Threshold Review**: Investigate why more updates are happening
4. **Seed Propagation**: Implement seed update on reference change
5. **Visualization**: Clarify what's displayed during vs after analysis
