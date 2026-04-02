/**
 * DIC2D workflow for CPPXDIC
 * Complete implementation of the MATLAB 2D DIC stage
 * Handles the full 2D DIC analysis pipeline
 */

#ifndef STEP_D_WORKFLOW_H
#define STEP_D_WORKFLOW_H

#include "parameters.h"
#include "image_processor.h"
#include "roi_manager.h"
#include "config.h"
#include "cppxdic/pipeline/dic2d_frame_preparer.h"
#include "cppxdic/pipeline/dic2d_output_formatter.h"
#include <ncorr.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cppxdic {

/**
 * Dic2DWorkflow class
 * Implements the complete 2D DIC analysis workflow
 * Equivalent to stepD_2DDIC.m in Matlab
 */
class Dic2DWorkflow {
public:
    /**
     * Constructor
     * 
     * @param config Global configuration
     */
    explicit Dic2DWorkflow(const Config& config);
    
    /**
     * Execute the DIC2D workflow
     * Main entry point for 2D DIC processing
     * 
     * @param trial Trial ID string (e.g., "005")
     * @param stereopair Stereo pair number (1 or 2)
     * @return Tuple of (outputPath, pairOrder, pairForced)
     */
    std::tuple<std::string, std::vector<int>, bool> execute(const std::string& trial,
                                                            int stereopair);
    
private:
    const Config& config_;
    BaseParameters base_params_;
    StepParameters step1_params_;      // Tracking camera 1
    StepParameters step2_params_;      // Tracking camera 2
    StepParameters step1_2_params_;    // Matching between cameras
    ProtocolInfo protocol_info_;
    
    /**
     * Setup base parameters for the current trial
     * 
     * @param trial Trial ID
     * @param stereopair Stereo pair number
     * @param reftrial Reference trial ID
     */
    void setupBaseParameters(const std::string& trial, 
                            int stereopair,
                            const std::string& reftrial);
    
    /**
     * Setup step parameters (DIC settings)
     */
    void setupStepParameters();
    
    /**
     * Load protocol information from MAT file
     * 
     * @return Success status
     */
    bool loadProtocol();
    
    /**
     * Determine reference trial based on phase and conditions
     * 
     * @param trial Current trial ID
     * @return Reference trial ID
     */
    std::string determineReferenceTrial(const std::string& trial);
    
    /**
     * Import video frames for current trial
     * 
     * @param trial Trial ID
     * @param stereopair Stereo pair number
     * @param cam_first_raw Output: first camera frames
     * @param cam_second_raw Output: second camera frames
     * @return Success status
     */
    bool importVideoFrames(const std::string& trial,
                          int stereopair,
                          std::vector<cv::Mat>& cam_first_raw,
                          std::vector<cv::Mat>& cam_second_raw);
    
    /**
     * Perform image saturation
     * 
     * @param cam_first_raw Input: first camera raw frames
     * @param cam_second_raw Input: second camera raw frames
     * @param cam_first_satur Output: saturated first camera frames
     * @param cam_second_satur Output: saturated second camera frames
     */
    void performSaturation(const std::vector<cv::Mat>& cam_first_raw,
                          const std::vector<cv::Mat>& cam_second_raw,
                          std::vector<cv::Mat>& cam_first_satur,
                          std::vector<cv::Mat>& cam_second_satur);
    
    /**
     * Initialize ROI and seed points
     * Loads or creates ROI mask and seed points, performs matching if needed
     * 
     * @param cam_first_satur First camera saturated frames
     * @param refmask_REF Output: reference ROI mask
     * @param refmask_trial Output: trial ROI mask
     * @param ref_seed_point Output: reference seed point
     * @param initial_seed_point_set1 Output: initial seed for camera 1
     * @return Success status
     */
    bool initializeROIAndSeed(const std::vector<cv::Mat>& cam_first_satur,
                             cv::Mat& refmask_REF,
                             cv::Mat& refmask_trial,
                             SeedPoint& ref_seed_point,
                             SeedPoint& initial_seed_point_set1);

    /**
     * Perform matching between cameras at initial frame
     * 
     * @param cam_ref First camera saturated frames
     * @param cam_cur Second camera saturated frames
     * @param refmask_ref Trial ROI mask
     * @param ncorr_matching_path Path to ncorr matching results
     * @param message Message to display
     * @param initial_seed_point_ref Initial seed for camera 1
     * @param refmask_cur_matched Output: matched trial ROI mask
     * @param initial_seed_point_cur Output: initial seed for camera 2
     * @return Success status
     */
    bool matchingInitialFrame(const std::vector<cv::Mat>& cam_ref,
        const std::vector<cv::Mat>& cam_cur,
        const cv::Mat& refmask_ref,
        const std::string ncorr_matching_path,
        const std::string message,
        const SeedPoint& initial_seed_point_ref,
        cv::Mat& refmask_cur_matched,
        SeedPoint& initial_seed_point_cur);
    
    /**
     * Perform matching between cameras at initial frame
     * 
     * @param cam_first_satur First camera saturated frames
     * @param cam_second_satur Second camera saturated frames
     * @param refmask_trial Trial ROI mask
     * @param initial_seed_point_set1 Initial seed for camera 1
     * @param refmask_trial_matched Output: matched trial ROI mask
     * @param initial_seed_point_set2 Output: initial seed for camera 2
     * @return Success status
     */
    bool performMatching(const std::vector<cv::Mat>& cam_first_satur,
                        const std::vector<cv::Mat>& cam_second_satur,
                        cv::Mat& refmask_trial,
                        SeedPoint& initial_seed_point_set1,
                        cv::Mat& refmask_trial_matched,
                        SeedPoint& initial_seed_point_set2);
    
                        /**
     * Perform tracking for a camera
     * 
     * @param tracking_number Tracking number (1 or 2)
     * @param cam_frames Filtered camera frames
     * @param refmask Trial ROI mask
     * @param initial_seed_point Initial seed point
     * @return Success status
     */
    bool performTracking(const int tracking_number,
                         const std::vector<cv::Mat>& cam_frames,
                         const cv::Mat& refmask,
                         const SeedPoint& initial_seed_point);

    /**
     * Perform tracking for camera 1
     * 
     * @param cam_first Filtered first camera frames
     * @param refmask_trial Trial ROI mask
     * @param initial_seed_point_set1 Initial seed point
     * @return Success status
     */
    bool performTracking1(const std::vector<cv::Mat>& cam_first,
                         const cv::Mat& refmask_trial,
                         const SeedPoint& initial_seed_point_set1);
    
    /**
     * Perform tracking for camera 2
     * 
     * @param cam_second Filtered second camera frames
     * @param refmask_trial_matched Matched trial ROI mask
     * @param initial_seed_point_set2 Initial seed point
     * @return Success status
     */
    bool performTracking2(const std::vector<cv::Mat>& cam_second,
                         const cv::Mat& refmask_trial_matched,
                         const SeedPoint& initial_seed_point_set2);
    
    /**
     * Apply image filtering (filter_like_ben)
     * 
     * @param cam_first_satur Saturated first camera frames
     * @param cam_second_satur Saturated second camera frames
     * @param refmask_trial Trial ROI mask
     * @param cam_first Output: filtered first camera frames
     * @param cam_second Output: filtered second camera frames
     */
    void applyImageFiltering(const std::vector<cv::Mat>& cam_first_satur,
                            const std::vector<cv::Mat>& cam_second_satur,
                            const cv::Mat& refmask_trial,
                            std::vector<cv::Mat>& cam_first,
                            std::vector<cv::Mat>& cam_second);
    
    /**
     * Save trial information to MAT file
     * 
     * @param trial Trial ID
     * @param stereopair Stereo pair number
     * @param num_frames Number of frames
     */
    void saveTrialInfo(const std::string& trial, int stereopair, int num_frames);
    
    /**
     * Format and save final output
     * 
     * @param trial Trial ID
     * @param stereopair Stereo pair number
     * @param pairOrder Trial-level stitch order metadata
     * @param pairForced Trial-level forced-stitch metadata
     */
    void formatOutput(const std::string& trial,
                      int stereopair,
                      const std::vector<int>& pairOrder,
                      bool pairForced);

    /**
     * Map an ROI mask and seed point through the first displacement field.
     */
    bool updateMaskAndSeedFromOutput(const cv::Mat& input_mask,
                                     const SeedPoint& input_seed,
                                     const ncorr::DIC_analysis_output& dic_output,
                                     cv::Mat& output_mask,
                                     SeedPoint& output_seed) const;

    /**
     * Write a compact debug panel for matching stages.
     */
    void writeMatchingDebugPanel(const std::string& stage_name,
                                 const cv::Mat& ref_img,
                                 const cv::Mat& cur_img,
                                 const cv::Mat& mask_before,
                                 const cv::Mat& mask_after,
                                 const SeedPoint& seed_before,
                                 const SeedPoint& seed_after,
                                 const ncorr::DIC_analysis_output& dic_output) const;
    
    /**
     * Run NCorr DIC analysis
     * 
     * @param ref_img Reference image
     * @param cur_imgs Current images
     * @param roi_mask ROI mask
     * @param seed_point Seed point
     * @param step_params Step parameters
     * @param output_path Output file path
     * @param go_parallel Whether to use parallel processing
     * @return DIC analysis output
     */
    ncorr::DIC_analysis_output runNcorrAnalysis(const cv::Mat& ref_img,
                                                const std::vector<cv::Mat>& cur_imgs,
                                                const cv::Mat& roi_mask,
                                                const SeedPoint& seed_point,
                                                const StepParameters& step_params,
                                                const std::string& output_path,
                                                const bool go_parallel,
                                                const bool use_no_update = false);
    
};

using StepDWorkflow = Dic2DWorkflow;

} // namespace cppxdic

#endif // STEP_D_WORKFLOW_H
