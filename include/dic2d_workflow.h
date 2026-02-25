/**
 * DIC 2D Workflow (Step D) for CPPXDIC
 * Complete implementation of 2D DIC analysis pipeline
 * Refactored from step_d_workflow.h with clearer structure
 * 
 * This workflow processes stereo video pairs to compute 2D displacement fields
 * and generates intermediate cache files for subsequent 3D reconstruction steps.
 * 
 * The workflow produces cached ncorr results that are used by Step E for 3D reconstruction.
 * These cached results are essential for efficient 3D point cloud generation in Step E.
 */

#ifndef DIC2D_WORKFLOW_H
#define DIC2D_WORKFLOW_H

#include "config.h"
#include "parameters.h"
#include <ncorr.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cppxdic {

/**
 * Input requirements for DIC 2D workflow
 */
struct DIC2DInputs {
    std::string trial;                  // Trial ID string (e.g., "005")
    int stereopair;                     // Stereo pair number (1 or 2)
    std::string reference_trial;        // Reference trial for ROI/seed
    
    // Paths (validated)
    std::string video_path_cam1;        // Path to camera 1 frames
    std::string video_path_cam2;        // Path to camera 2 frames
    std::string roi_file;               // Path to ROI mask file
    std::string seed_file;              // Path to seed point file
    std::string output_path;            // Output directory
    
    bool isValid() const;
};

/**
 * Output from DIC 2D workflow
 */
struct DIC2DOutputs {
    std::string output_path;            // Path where results are saved
    std::vector<int> pair_order;        // Pair processing order
    bool pair_forced;                   // Whether pair order was forced
    
    // Cache file paths (for Step E)
    std::string ncorr_cam1_cache;       // Camera 1 tracking results (.bin)
    std::string ncorr_cam2_cache;       // Camera 2 tracking results (.bin)
    std::string ncorr_matching_cache;   // Camera matching results (.bin)
    
    // Statistics
    int num_frames_processed;
    int num_points;
    
    bool isValid() const { return !output_path.empty(); }
};

/**
 * DIC2DWorkflow class (Step D)
 * 
 * Responsibilities:
 * - Import and preprocess video frames
 * - Initialize ROI and seed points
 * - Perform inter-camera matching
 * - Track deformation in both cameras
 * - Generate DIC2DpairResults output
 * 
 * Inputs (validated in constructor):
 * - Config with valid DIC parameters
 * - DIC2DInputs with valid paths
 * 
 * Outputs:
 * - Cached ncorr results (.bin files)
 * - DIC2DpairResults MAT file (optional)
 */
class DIC2DWorkflow {
public:
    /**
     * Constructor - validates required inputs
     * 
     * @param config Global configuration
     * @throws std::runtime_error if config is invalid
     */
    explicit DIC2DWorkflow(const Config& config);
    
    // =========================================================================
    // Main Entry Point
    // =========================================================================
    
    /**
     * Execute DIC 2D analysis for a trial/pair
     * 
     * @param trial Trial ID string (e.g., "005")
     * @param stereopair Stereo pair number (1 or 2)
     * @return DIC2DOutputs with results, or invalid outputs on failure
     */
    DIC2DOutputs execute(const std::string& trial, int stereopair);
    
    /**
     * Execute with explicit inputs (for testing)
     * 
     * @param inputs Validated input structure
     * @return DIC2DOutputs with results
     */
    DIC2DOutputs execute(const DIC2DInputs& inputs);
    
    // =========================================================================
    // Milestone Functions (for testing)
    // =========================================================================
    
    /**
     * Import video frames for a trial
     * 
     * @param trial Trial ID
     * @param stereopair Stereo pair number
     * @param cam1_frames Output: camera 1 frames
     * @param cam2_frames Output: camera 2 frames
     * @return true if import successful
     */
    bool importVideoFrames(const std::string& trial, int stereopair,
                          std::vector<cv::Mat>& cam1_frames,
                          std::vector<cv::Mat>& cam2_frames);
    
    /**
     * Apply saturation to frames
     * 
     * @param raw_frames Input raw frames
     * @param limit_grayscale Grayscale limit for saturation
     * @return Saturated frames
     */
    std::vector<cv::Mat> applySaturation(const std::vector<cv::Mat>& raw_frames,
                                         int limit_grayscale);
    
    /**
     * Initialize ROI and seed point
     * 
     * @param reference_frame First frame for ROI reference
     * @param roi_file Path to ROI file
     * @param seed_file Path to seed file
     * @param roi_mask Output: ROI mask
     * @param seed_point Output: seed point
     * @return true if initialization successful
     */
    bool initializeROIAndSeed(const cv::Mat& reference_frame,
                             const std::string& roi_file,
                             const std::string& seed_file,
                             cv::Mat& roi_mask,
                             SeedPoint& seed_point);
    
    /**
     * Perform matching between reference and current trial
     * 
     * @param ref_frame Reference trial frame
     * @param cur_frame Current trial frame
     * @param roi_mask ROI mask
     * @param seed_in Input seed point
     * @param output_path Path to save matching results
     * @param seed_out Output: transformed seed point
     * @return true if matching successful
     */
    bool performRefToTrialMatching(const cv::Mat& ref_frame,
                                   const cv::Mat& cur_frame,
                                   const cv::Mat& roi_mask,
                                   const SeedPoint& seed_in,
                                   const std::string& output_path,
                                   SeedPoint& seed_out);
    
    /**
     * Perform inter-camera matching (cam1 -> cam2)
     * 
     * @param cam1_frame Camera 1 frame
     * @param cam2_frame Camera 2 frame
     * @param roi_mask ROI mask
     * @param seed_cam1 Camera 1 seed point
     * @param output_path Path to save matching results
     * @param roi_cam2 Output: transformed ROI for camera 2
     * @param seed_cam2 Output: transformed seed for camera 2
     * @return true if matching successful
     */
    bool performInterCameraMatching(const cv::Mat& cam1_frame,
                                    const cv::Mat& cam2_frame,
                                    const cv::Mat& roi_mask,
                                    const SeedPoint& seed_cam1,
                                    const std::string& output_path,
                                    cv::Mat& roi_cam2,
                                    SeedPoint& seed_cam2);
    
    /**
     * Perform temporal tracking for a camera
     * 
     * @param frames All frames for this camera
     * @param roi_mask ROI mask
     * @param seed_point Initial seed point
     * @param output_path Path to save tracking results
     * @param camera_number Camera number (for logging)
     * @return true if tracking successful
     */
    bool performTracking(const std::vector<cv::Mat>& frames,
                        const cv::Mat& roi_mask,
                        const SeedPoint& seed_point,
                        const std::string& output_path,
                        int camera_number);
    
    /**
     * Apply image filtering (Ben's filter)
     * 
     * @param frames Input frames
     * @param roi_mask ROI mask
     * @return Filtered frames
     */
    std::vector<cv::Mat> applyImageFiltering(const std::vector<cv::Mat>& frames,
                                             const cv::Mat& roi_mask);
    
    /**
     * Format and save final output
     * 
     * @param trial Trial ID
     * @param stereopair Stereo pair number
     * @return true if output saved successfully
     */
    bool formatOutput(const std::string& trial, int stereopair);
    
    // =========================================================================
    // Checkpoint Functions
    // =========================================================================
    
    /**
     * Check if tracking results exist for a camera
     * 
     * @param output_path Base output path
     * @param camera_number Camera number
     * @return true if checkpoint exists
     */
    bool hasTrackingCheckpoint(const std::string& output_path, int camera_number);
    
    /**
     * Check if matching results exist
     * 
     * @param matching_path Path to matching file
     * @return true if checkpoint exists
     */
    bool hasMatchingCheckpoint(const std::string& matching_path);
    
    // =========================================================================
    // Utility Functions
    // =========================================================================
    
    /**
     * Build input structure from config and trial info
     * 
     * @param trial Trial ID
     * @param stereopair Stereo pair number
     * @param reference_trial Reference trial ID
     * @return DIC2DInputs structure
     */
    DIC2DInputs buildInputs(const std::string& trial, int stereopair,
                           const std::string& reference_trial);

private:
    const Config& config_;
    BaseParameters base_params_;
    StepParameters tracking_params_;
    StepParameters matching_params_;
    
    /** Setup DIC parameters */
    void setupParameters();
    
    /** Run NCorr analysis */
    ncorr::DIC_analysis_output runNcorrAnalysis(
        const cv::Mat& ref_img,
        const std::vector<cv::Mat>& cur_imgs,
        const cv::Mat& roi_mask,
        const SeedPoint& seed_point,
        const StepParameters& params,
        const std::string& output_path,
        bool parallel);
};

} // namespace cppxdic

#endif // DIC2D_WORKFLOW_H
