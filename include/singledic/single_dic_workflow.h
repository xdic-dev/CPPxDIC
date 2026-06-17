/**
 * @file single_dic_workflow.h
 * @brief Single-camera 2D DIC step-D pipeline (no stereo, no 3D).
 *
 * This is the C++ analogue of Viktoriia's single-camera "sliding analysis"
 * step D. Reference MATLAB files (read-only, under
 * Tools/MultiDIC/lib_script/sliding_analysis/viktoriia_script_analysis/):
 *
 *   - process_single_trial_ncorr.m  — top-level single-trial driver this class
 *                                      mirrors (read video, saturate, ROI/seed,
 *                                      optional matching, single tracking pass).
 *   - draw_ref_roi_single_trial.m   — ROI on the reference (here: load-or-full).
 *   - draw_ref_seed_single.m        — seed on the reference (here: load-or-center).
 *   - ncorr_matching2ref_single.m   — optional matching-to-reference-trial pass.
 *
 * Contrast with the stereo step D (Tools/MultiDIC/main_script/stepD_2DDIC.m and
 * the C++ cppxdic::StepDWorkflow): that runs matching-between-cameras + two
 * tracking passes (cam1, cam2) feeding stepE/stepF 3D reconstruction. The
 * single-camera flow keeps only ONE tracking pass and stops there.
 *
 * Reuse: this class deliberately leans on the existing CPPxDIC building blocks
 * (cppxdic::ImageProcessor, cppxdic::ROIManager, Utils) and drives the DIC
 * engine through CppNCorr's in-memory ncorr::NcorrSession, so it needs no temp
 * PNG round-trip and no stereo-specific code.
 */

#ifndef SINGLEDIC_SINGLE_DIC_WORKFLOW_H
#define SINGLEDIC_SINGLE_DIC_WORKFLOW_H

#include "singledic/singledic_config.h"

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace singledic {

/// One tracked frame's reduced-grid DIC result (Lagrangian, pixels).
struct FrameResult {
    int grid_width = 0;           ///< Displacement grid width (reduced).
    int grid_height = 0;          ///< Displacement grid height (reduced).
    std::vector<double> u;        ///< U displacement (px), row-major, NaN outside ROI.
    std::vector<double> v;        ///< V displacement (px), row-major.
    std::vector<double> corrcoef; ///< Correlation coefficient, row-major.
    bool valid = false;           ///< True if the frame correlated successfully.
};

/// Full single-trial output: one reference + N tracked frames.
struct SingleDicResult {
    cv::Size image_size;             ///< Reference frame pixel size.
    cv::Mat roi_mask;                ///< ROI used (CV_8U, non-zero = inside).
    int subset_spacing = 1;          ///< Spacing used (grid factor = spacing+1).
    std::vector<int> seed_pw;        ///< Seed point [x, y] in pixels.
    std::vector<FrameResult> frames; ///< Per-frame DIC results (index 0 = ref vs frame 1...).
    bool ok = false;                 ///< Overall success.
    std::string message;             ///< Human-readable status.
};

/**
 * @brief Drives the single-camera 2D-DIC step-D pipeline for one trial.
 */
class SingleDicWorkflow {
public:
    explicit SingleDicWorkflow(const SinglediConfig& config);

    /**
     * @brief Run the full pipeline: import, saturate, (filter), ROI/seed,
     *        (optional matching), single tracking pass, write outputs.
     * @return Aggregated result; `ok` is false on any fatal error.
     */
    SingleDicResult run();

private:
    SinglediConfig cfg_;

    // --- pipeline stages (each mirrors a MATLAB step) ----------------------

    /// Read the trial's image sequence (video file or numbered image folder).
    /// Mirrors readvid() in process_single_trial_ncorr.m. Honours the
    /// [idx_frame_start, idx_frame_end] window, frame_jump, and backward flip.
    bool importFrames(std::vector<cv::Mat>& frames) const;

    /// satur(satur(im,'level',high),'method','low','level',low). Mirrors the
    /// im_satur line in process_single_trial_ncorr.m.
    std::vector<cv::Mat> saturate(const std::vector<cv::Mat>& frames) const;

    /// Optional bandpass + percentile-normalise (filter_like_ben style).
    std::vector<cv::Mat> maybeFilter(const std::vector<cv::Mat>& frames,
                                     const cv::Mat& roi_mask) const;

    /// Load ROI mask (REF_MASK_*.mat) or fall back to a full-frame ROI.
    cv::Mat loadOrCreateRoi(const cv::Mat& reference) const;

    /// Load seed (REF_SEED_*.mat) or fall back to the ROI centre.
    std::vector<int> loadOrCreateSeed(const cv::Mat& roi_mask) const;

    /// Run the single tracking pass (reference frame vs all current frames)
    /// through ncorr::NcorrSession. Mirrors the ncorr_dic_rewrited() call.
    bool track(const std::vector<cv::Mat>& frames, const cv::Mat& roi_mask,
               const std::vector<int>& seed_pw, SingleDicResult& out) const;

    // --- output writers ----------------------------------------------------

    /// Write a CSV of per-frame tracked feature trajectories + corrcoef.
    bool writeCsv(const SingleDicResult& res) const;

    /// Write a MATLAB-compatible .mat sidecar (reuses cppxdic::MatWriter where
    /// the ncorr DIC_analysis_output is available; otherwise a compact array
    /// dump of u/v/corrcoef grids).
    bool writeMat(const SingleDicResult& res) const;
};

} // namespace singledic

#endif // SINGLEDIC_SINGLE_DIC_WORKFLOW_H
