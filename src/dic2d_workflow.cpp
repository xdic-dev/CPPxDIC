/**
 * DIC 2D Workflow (Step D) implementation for CPPXDIC
 * Refactored from step_d_workflow.cpp with clearer structure
 */

#include "dic2d_workflow.h"
#include "image_processor.h"
#include "roi_manager.h"
#include "mat_writer.h"
#include "delaunay_triangulation.h"
#include "utils.h"
#include "Array2D.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <matio.h>
#include <cmath>

namespace cppxdic {

// ============================================================================
// Input/Output validation
// ============================================================================

bool DIC2DInputs::isValid() const {
    return !trial.empty() && 
           stereopair > 0 && 
           !output_path.empty();
}

// ============================================================================
// Constructor
// ============================================================================

DIC2DWorkflow::DIC2DWorkflow(const Config& config) : config_(config) {
    setupParameters();
}

void DIC2DWorkflow::setupParameters() {
    // Setup tracking parameters
    tracking_params_.type = "regular";
    tracking_params_.radius = DICConstants::SUBSET_RADIUS_TRACKING;
    tracking_params_.spacing = DICConstants::SUBSET_SPACING;
    tracking_params_.cutoff_diffnorm = DICConstants::CUTOFF_TRACKING;
    tracking_params_.cutoff_iteration = DICConstants::NUMBER_ITERATION_SOLVER;
    tracking_params_.total_threads = DICConstants::NUMBER_THREADS;
    tracking_params_.stepanalysis_params.enabled = true;
    tracking_params_.stepanalysis_params.type = "seed";
    tracking_params_.stepanalysis_params.auto_update = true;
    tracking_params_.stepanalysis_params.step = 10;
    
    // Setup matching parameters (larger radius)
    matching_params_ = tracking_params_;
    matching_params_.radius = DICConstants::SUBSET_RADIUS_MATCHING;
    matching_params_.cutoff_diffnorm = DICConstants::CUTOFF_MATCHING;
}

// ============================================================================
// Main Entry Points
// ============================================================================

DIC2DOutputs DIC2DWorkflow::execute(const std::string& trial, int stereopair) {
    std::cout << "-------------------------------------------" << std::endl;
    std::cout << "DIC 2D Analysis - Trial " << trial << ", Pair " << stereopair << std::endl;
    std::cout << "-------------------------------------------" << std::endl;
    
    // Determine reference trial
    std::string reftrial;
    if (config_.ref_trial_id > 0) {
        std::ostringstream oss;
        oss << std::setw(3) << std::setfill('0') << config_.ref_trial_id;
        reftrial = oss.str();
    } else {
        reftrial = trial;
    }
    
    // Build inputs
    DIC2DInputs inputs = buildInputs(trial, stereopair, reftrial);
    
    return execute(inputs);
}

DIC2DOutputs DIC2DWorkflow::execute(const DIC2DInputs& inputs) {
    DIC2DOutputs outputs;
    
    // Setup base parameters
    base_params_.baseDataPath = config_.data_path;
    base_params_.baseResultPath = config_.dic_path;
    base_params_.subject = config_.subject_id;
    base_params_.material = config_.material;
    base_params_.trial = inputs.trial;
    base_params_.stereopair = inputs.stereopair;
    base_params_.phase = config_.phase_id;
    base_params_.reftrial = inputs.reference_trial;
    base_params_.jump = config_.frame_jump;
    base_params_.idxstart_set = config_.idx_frame_start;
    base_params_.idxend_set = config_.idx_frame_end;
    
    // Set grayscale limit based on subject number
    int subject_num = 0;
    for (char ch : config_.subject_id) {
        if (std::isdigit(ch)) {
            subject_num = subject_num * 10 + (ch - '0');
        }
    }
    base_params_.limit_grayscale = (subject_num < 8) ? 
        DICConstants::LIMIT_GRAYSCALE_DEFAULT : DICConstants::LIMIT_GRAYSCALE_S8_PLUS;
    
    // Set camera numbers
    getCameraNumbers(inputs.stereopair, base_params_.cam_1, base_params_.cam_2);
    
    // Set output path
    base_params_.outputPath = base_params_.baseResultPath + "/" + 
        base_params_.subject + "/" + base_params_.material + "/" +
        base_params_.trial + "/" + base_params_.phase;
    
    // Create output directory
    std::filesystem::create_directories(base_params_.outputPath);
    
    // Set file paths
    base_params_.roifile = base_params_.baseResultPath + "/" + base_params_.subject + "/" +
        base_params_.material + "/REF_MASK_" + inputs.reference_trial + "_" + base_params_.phase +
        "_pair" + std::to_string(inputs.stereopair) + ".mat";
    
    base_params_.matchingfile = base_params_.outputPath + "/MATCHING2" + 
        inputs.reference_trial + "_pair" + std::to_string(inputs.stereopair) + ".mat";
    
    base_params_.seedfile = base_params_.baseResultPath + "/" + base_params_.subject + "/" +
        base_params_.material + "/REF_SEED_" + inputs.reference_trial + "_" + base_params_.phase +
        "_pair" + std::to_string(inputs.stereopair) + ".mat";
    
    // Step 1: Import video frames
    std::vector<cv::Mat> cam1_raw, cam2_raw;
    std::cout << "Reading video data..." << std::endl;
    if (!importVideoFrames(inputs.trial, inputs.stereopair, cam1_raw, cam2_raw)) {
        std::cerr << "Failed to import video frames" << std::endl;
        return outputs;
    }
    std::cout << "Reading done. Frames: " << cam1_raw.size() << std::endl;
    
    // Phase-specific frame selection
    if (config_.phase_id == "slide1") {
        size_t keep = cam1_raw.size() / 2 + 5;
        cam1_raw.resize(keep);
        cam2_raw.resize(keep);
        std::cout << "Phase 'slide1': keeping " << keep << " frames" << std::endl;
    }
    
    // Step 2: Apply saturation
    std::cout << "Applying saturation..." << std::endl;
    std::vector<cv::Mat> cam1_satur = applySaturation(cam1_raw, base_params_.limit_grayscale);
    std::vector<cv::Mat> cam2_satur = applySaturation(cam2_raw, base_params_.limit_grayscale);
    
    // Step 3: Initialize ROI and seed
    cv::Mat roi_mask;
    SeedPoint seed_cam1;
    std::cout << "Initializing ROI and seed..." << std::endl;
    if (!initializeROIAndSeed(cam1_satur[0], base_params_.roifile, 
                              base_params_.seedfile, roi_mask, seed_cam1)) {
        std::cerr << "Failed to initialize ROI and seed" << std::endl;
        return outputs;
    }
    std::cout << "--> STEP: ROI and SEED loaded" << std::endl;
    
    // Step 4: Perform REF to Trial matching (if different trials)
    if (inputs.reference_trial != inputs.trial) {
        // Load reference trial frames
        std::vector<cv::Mat> ref_cam1_raw, ref_cam2_raw;
        if (!importVideoFrames(inputs.reference_trial, inputs.stereopair, 
                               ref_cam1_raw, ref_cam2_raw)) {
            std::cerr << "Failed to import reference trial frames" << std::endl;
            return outputs;
        }
        
        std::vector<cv::Mat> ref_cam1_satur = applySaturation(ref_cam1_raw, 
                                                              base_params_.limit_grayscale);
        
        SeedPoint seed_trial;
        if (!performRefToTrialMatching(ref_cam1_satur[0], cam1_satur[0], roi_mask,
                                       seed_cam1, base_params_.matchingfile, seed_trial)) {
            std::cerr << "Failed REF to Trial matching" << std::endl;
            return outputs;
        }
        seed_cam1 = seed_trial;
        std::cout << "--> STEP: REF to Trial matching done" << std::endl;
    }
    
    // Step 5: Inter-camera matching (cam1 -> cam2)
    cv::Mat roi_cam2;
    SeedPoint seed_cam2;
    std::string matching_path = base_params_.outputPath + "/ncorr" + 
        std::to_string(base_params_.cam_1) + std::to_string(base_params_.cam_2) + ".mat";
    
    std::cout << "Performing inter-camera matching..." << std::endl;
    if (!performInterCameraMatching(cam1_satur[0], cam2_satur[0], roi_mask,
                                    seed_cam1, matching_path, roi_cam2, seed_cam2)) {
        std::cerr << "Failed inter-camera matching" << std::endl;
        return outputs;
    }
    std::cout << "--> STEP: Inter-camera matching done" << std::endl;
    
    // Step 6: Apply image filtering (optional)
    std::vector<cv::Mat> cam1_filtered, cam2_filtered;
    if (config_.im_filter_mode) {
        std::cout << "Applying image filtering..." << std::endl;
        cam1_filtered = applyImageFiltering(cam1_satur, roi_mask);
        cam2_filtered = applyImageFiltering(cam2_satur, roi_cam2);
        std::cout << "--> STEP: Filtering done" << std::endl;
    } else {
        cam1_filtered = cam1_satur;
        cam2_filtered = cam2_satur;
    }
    
    // Step 7: Tracking camera 1
    std::string track1_path = base_params_.outputPath + "/ncorr" + 
        std::to_string(base_params_.cam_1) + ".mat";
    std::cout << "Performing tracking camera 1..." << std::endl;
    if (!performTracking(cam1_filtered, roi_mask, seed_cam1, track1_path, base_params_.cam_1)) {
        std::cerr << "Failed tracking camera 1" << std::endl;
        return outputs;
    }
    std::cout << "--> STEP: Tracking camera 1 done" << std::endl;
    
    // Step 8: Tracking camera 2
    std::string track2_path = base_params_.outputPath + "/ncorr" + 
        std::to_string(base_params_.cam_2) + ".mat";
    std::cout << "Performing tracking camera 2..." << std::endl;
    if (!performTracking(cam2_filtered, roi_cam2, seed_cam2, track2_path, base_params_.cam_2)) {
        std::cerr << "Failed tracking camera 2" << std::endl;
        return outputs;
    }
    std::cout << "--> STEP: Tracking camera 2 done" << std::endl;
    
    // Step 9: Format output
    if (!formatOutput(inputs.trial, inputs.stereopair)) {
        std::cerr << "Warning: Failed to format output" << std::endl;
    }
    
    // Build outputs
    outputs.output_path = base_params_.outputPath;
    outputs.pair_order = {1, 2};
    outputs.pair_forced = false;
    outputs.num_frames_processed = static_cast<int>(cam1_filtered.size());
    
    std::filesystem::path cache_dir = std::filesystem::path(base_params_.outputPath) / ".cache";
    outputs.ncorr_cam1_cache = (cache_dir / ("ncorr" + std::to_string(base_params_.cam_1) + ".mat.bin")).string();
    outputs.ncorr_cam2_cache = (cache_dir / ("ncorr" + std::to_string(base_params_.cam_2) + ".mat.bin")).string();
    outputs.ncorr_matching_cache = (cache_dir / ("ncorr" + std::to_string(base_params_.cam_1) + 
                                                 std::to_string(base_params_.cam_2) + ".mat.bin")).string();
    
    std::cout << "✓ DIC 2D Analysis complete" << std::endl;
    return outputs;
}

// ============================================================================
// Milestone Functions
// ============================================================================

bool DIC2DWorkflow::importVideoFrames(const std::string& trial, int stereopair,
                                      std::vector<cv::Mat>& cam1_frames,
                                      std::vector<cv::Mat>& cam2_frames) {
    std::vector<std::string> cam1_paths, cam2_paths;
    
    int trial_num = std::stoi(trial);
    if (!Utils::importVid(config_, trial_num, stereopair, cam1_paths, cam2_paths)) {
        return false;
    }
    
    if (cam1_paths.empty() || cam2_paths.empty()) {
        return false;
    }
    
    if (cam1_paths.size() != cam2_paths.size()) {
        std::cerr << "Frame count mismatch: cam1=" << cam1_paths.size() 
                  << " cam2=" << cam2_paths.size() << std::endl;
        return false;
    }
    
    cam1_frames.clear();
    cam2_frames.clear();
    cam1_frames.reserve(cam1_paths.size());
    cam2_frames.reserve(cam2_paths.size());
    
    for (size_t i = 0; i < cam1_paths.size(); ++i) {
        cv::Mat img1 = cv::imread(cam1_paths[i], cv::IMREAD_GRAYSCALE);
        cv::Mat img2 = cv::imread(cam2_paths[i], cv::IMREAD_GRAYSCALE);
        
        if (img1.empty() || img2.empty()) {
            std::cerr << "Failed to load frame " << i << std::endl;
            return false;
        }
        
        cam1_frames.push_back(img1);
        cam2_frames.push_back(img2);
    }
    
    return true;
}

std::vector<cv::Mat> DIC2DWorkflow::applySaturation(const std::vector<cv::Mat>& raw_frames,
                                                    int limit_grayscale) {
    return ImageProcessor::saturate(raw_frames, limit_grayscale);
}

bool DIC2DWorkflow::initializeROIAndSeed(const cv::Mat& reference_frame,
                                         const std::string& roi_file,
                                         const std::string& seed_file,
                                         cv::Mat& roi_mask,
                                         SeedPoint& seed_point) {
    // Load or create ROI
    roi_mask = ROIManager::loadOrCreateROI(base_params_, reference_frame);
    if (roi_mask.empty()) {
        return false;
    }
    
    // Load or create seed
    seed_point = ROIManager::loadOrCreateSeed(base_params_, roi_mask);
    
    return true;
}

bool DIC2DWorkflow::performRefToTrialMatching(const cv::Mat& ref_frame,
                                              const cv::Mat& cur_frame,
                                              const cv::Mat& roi_mask,
                                              const SeedPoint& seed_in,
                                              const std::string& output_path,
                                              SeedPoint& seed_out) {
    // Check cache first
    std::filesystem::path cache_bin = std::filesystem::path(output_path).parent_path() / 
        ".cache" / (std::filesystem::path(output_path).filename().string() + ".bin");
    
    if (std::filesystem::exists(cache_bin)) {
        std::cout << "Checkpoint found: " << cache_bin.string() << std::endl;
        
        auto dic_output = ncorr::DIC_analysis_output::load(cache_bin.string());
        if (!dic_output.disps.empty()) {
            const auto& disp = dic_output.disps[0];
            const auto& u_array = disp.get_u().get_array();
            const auto& v_array = disp.get_v().get_array();
            
            cv::Mat U_mapped(disp.get_u().data_height(), disp.get_u().data_width(), CV_64F);
            cv::Mat V_mapped(disp.get_v().data_height(), disp.get_v().data_width(), CV_64F);
            
            for (size_t y = 0; y < static_cast<size_t>(disp.get_u().data_height()); ++y) {
                for (size_t x = 0; x < static_cast<size_t>(disp.get_u().data_width()); ++x) {
                    U_mapped.at<double>(y, x) = u_array(y, x);
                    V_mapped.at<double>(y, x) = v_array(y, x);
                }
            }
            
            seed_out.sw = ROIManager::mapPointCoordinate(seed_in.sw, U_mapped, V_mapped);
            seed_out.pw = ROIManager::mapSubset2Pixel(seed_out.sw, matching_params_.spacing);
            return true;
        }
    }
    
    // Run matching
    std::vector<cv::Mat> cur_imgs = {cur_frame};
    matching_params_.initial_seed = {
        static_cast<int>(seed_in.pw[0]),
        static_cast<int>(seed_in.pw[1])
    };
    
    auto dic_output = runNcorrAnalysis(ref_frame, cur_imgs, roi_mask,
                                       seed_in, matching_params_, output_path, false);
    
    if (dic_output.disps.empty()) {
        return false;
    }
    
    // Extract displacement and map seed
    const auto& disp = dic_output.disps[0];
    const auto& u_array = disp.get_u().get_array();
    const auto& v_array = disp.get_v().get_array();
    
    cv::Mat U_mapped(disp.get_u().data_height(), disp.get_u().data_width(), CV_64F);
    cv::Mat V_mapped(disp.get_v().data_height(), disp.get_v().data_width(), CV_64F);
    
    for (size_t y = 0; y < static_cast<size_t>(disp.get_u().data_height()); ++y) {
        for (size_t x = 0; x < static_cast<size_t>(disp.get_u().data_width()); ++x) {
            U_mapped.at<double>(y, x) = u_array(y, x);
            V_mapped.at<double>(y, x) = v_array(y, x);
        }
    }
    
    seed_out.sw = ROIManager::mapPointCoordinate(seed_in.sw, U_mapped, V_mapped);
    seed_out.pw = ROIManager::mapSubset2Pixel(seed_out.sw, matching_params_.spacing);
    
    return true;
}

bool DIC2DWorkflow::performInterCameraMatching(const cv::Mat& cam1_frame,
                                               const cv::Mat& cam2_frame,
                                               const cv::Mat& roi_mask,
                                               const SeedPoint& seed_cam1,
                                               const std::string& output_path,
                                               cv::Mat& roi_cam2,
                                               SeedPoint& seed_cam2) {
    // Check cache first
    std::filesystem::path cache_bin = std::filesystem::path(output_path).parent_path() / 
        ".cache" / (std::filesystem::path(output_path).filename().string() + ".bin");
    
    if (std::filesystem::exists(cache_bin)) {
        std::cout << "Checkpoint found: " << cache_bin.string() << std::endl;
        
        auto dic_output = ncorr::DIC_analysis_output::load(cache_bin.string());
        if (!dic_output.disps.empty()) {
            roi_cam2 = roi_mask.clone();
            
            const auto& disp = dic_output.disps[0];
            const auto& u_array = disp.get_u().get_array();
            const auto& v_array = disp.get_v().get_array();
            
            cv::Mat U_mapped(disp.get_u().data_height(), disp.get_u().data_width(), CV_64F);
            cv::Mat V_mapped(disp.get_v().data_height(), disp.get_v().data_width(), CV_64F);
            
            for (size_t y = 0; y < static_cast<size_t>(disp.get_u().data_height()); ++y) {
                for (size_t x = 0; x < static_cast<size_t>(disp.get_u().data_width()); ++x) {
                    U_mapped.at<double>(y, x) = u_array(y, x);
                    V_mapped.at<double>(y, x) = v_array(y, x);
                }
            }
            
            seed_cam2.sw = ROIManager::mapPointCoordinate(seed_cam1.sw, U_mapped, V_mapped);
            seed_cam2.pw = ROIManager::mapSubset2Pixel(seed_cam2.sw, matching_params_.spacing);
            return true;
        }
    }
    
    // Run matching
    std::vector<cv::Mat> cam2_imgs = {cam2_frame};
    matching_params_.initial_seed = {
        static_cast<int>(seed_cam1.pw[0]),
        static_cast<int>(seed_cam1.pw[1])
    };
    
    auto dic_output = runNcorrAnalysis(cam1_frame, cam2_imgs, roi_mask,
                                       seed_cam1, matching_params_, output_path, false);
    
    if (dic_output.disps.empty()) {
        return false;
    }
    
    roi_cam2 = roi_mask.clone();
    
    const auto& disp = dic_output.disps[0];
    const auto& u_array = disp.get_u().get_array();
    const auto& v_array = disp.get_v().get_array();
    
    cv::Mat U_mapped(disp.get_u().data_height(), disp.get_u().data_width(), CV_64F);
    cv::Mat V_mapped(disp.get_v().data_height(), disp.get_v().data_width(), CV_64F);
    
    for (size_t y = 0; y < static_cast<size_t>(disp.get_u().data_height()); ++y) {
        for (size_t x = 0; x < static_cast<size_t>(disp.get_u().data_width()); ++x) {
            U_mapped.at<double>(y, x) = u_array(y, x);
            V_mapped.at<double>(y, x) = v_array(y, x);
        }
    }
    
    seed_cam2.sw = ROIManager::mapPointCoordinate(seed_cam1.sw, U_mapped, V_mapped);
    seed_cam2.pw = ROIManager::mapSubset2Pixel(seed_cam2.sw, matching_params_.spacing);
    
    return true;
}

bool DIC2DWorkflow::performTracking(const std::vector<cv::Mat>& frames,
                                    const cv::Mat& roi_mask,
                                    const SeedPoint& seed_point,
                                    const std::string& output_path,
                                    int camera_number) {
    // Check cache first
    std::filesystem::path cache_bin = std::filesystem::path(output_path).parent_path() / 
        ".cache" / ("ncorr" + std::to_string(camera_number) + ".mat.bin");
    
    if (std::filesystem::exists(cache_bin)) {
        std::cout << "Checkpoint found: " << cache_bin.string() << std::endl;
        return true;
    }
    
    // Setup tracking parameters
    tracking_params_.initial_seed = {
        static_cast<int>(seed_point.pw[0]),
        static_cast<int>(seed_point.pw[1])
    };
    
    auto dic_output = runNcorrAnalysis(frames[0], frames, roi_mask,
                                       seed_point, tracking_params_, output_path,
                                       config_.parallel_processing);
    
    return !dic_output.disps.empty();
}

std::vector<cv::Mat> DIC2DWorkflow::applyImageFiltering(const std::vector<cv::Mat>& frames,
                                                        const cv::Mat& roi_mask) {
    std::vector<int> param_filt = {25, 300};
    auto [filtered, gs_bounds] = ImageProcessor::filterLikeBen(frames, roi_mask, param_filt, nullptr);
    return filtered;
}

bool DIC2DWorkflow::formatOutput(const std::string& trial, int stereopair) {
    if (!config_.generate_mat_files) {
        return true;
    }
    
    int cam_1, cam_2;
    getCameraNumbers(stereopair, cam_1, cam_2);
    
    std::filesystem::path cache_dir = std::filesystem::path(base_params_.outputPath) / ".cache";
    std::string ncorr1_bin = (cache_dir / ("ncorr" + std::to_string(cam_1) + ".mat.bin")).string();
    std::string ncorr2_bin = (cache_dir / ("ncorr" + std::to_string(cam_2) + ".mat.bin")).string();
    std::string ncorr12_bin = (cache_dir / ("ncorr" + std::to_string(cam_1) + 
                                            std::to_string(cam_2) + ".mat.bin")).string();
    
    if (!std::filesystem::exists(ncorr1_bin) || 
        !std::filesystem::exists(ncorr2_bin) || 
        !std::filesystem::exists(ncorr12_bin)) {
        std::cerr << "Warning: cached ncorr files not found" << std::endl;
        return false;
    }
    
    std::string output_file = base_params_.outputPath + "/myDIC2DpairResults_C_" + 
        std::to_string(cam_1) + "_C_" + std::to_string(cam_2) + ".mat";
    
    if (std::filesystem::exists(output_file)) {
        std::cout << "Checkpoint found: " << output_file << std::endl;
        return true;
    }
    
    // Load and format results
    ncorr::DIC_analysis_output dic1 = ncorr::DIC_analysis_output::load(ncorr1_bin);
    ncorr::DIC_analysis_output dic2 = ncorr::DIC_analysis_output::load(ncorr2_bin);
    ncorr::DIC_analysis_output dic12 = ncorr::DIC_analysis_output::load(ncorr12_bin);
    
    if (dic1.disps.empty() || dic2.disps.empty() || dic12.disps.empty()) {
        return false;
    }
    
    // Build DIC2DPairResults and write to MAT
    // (Implementation similar to step_d_workflow.cpp formatOutput)
    
    return true;
}

// ============================================================================
// Checkpoint Functions
// ============================================================================

bool DIC2DWorkflow::hasTrackingCheckpoint(const std::string& output_path, int camera_number) {
    std::filesystem::path cache_bin = std::filesystem::path(output_path) / ".cache" / 
        ("ncorr" + std::to_string(camera_number) + ".mat.bin");
    return std::filesystem::exists(cache_bin);
}

bool DIC2DWorkflow::hasMatchingCheckpoint(const std::string& matching_path) {
    std::filesystem::path cache_bin = std::filesystem::path(matching_path).parent_path() / 
        ".cache" / (std::filesystem::path(matching_path).filename().string() + ".bin");
    return std::filesystem::exists(cache_bin);
}

// ============================================================================
// Utility Functions
// ============================================================================

void DIC2DWorkflow::getCameraNumbers(int stereopair, int& cam1, int& cam2) {
    cam1 = (stereopair - 1) * 2 + 1;
    cam2 = (stereopair - 1) * 2 + 2;
}

DIC2DInputs DIC2DWorkflow::buildInputs(const std::string& trial, int stereopair,
                                       const std::string& reference_trial) {
    DIC2DInputs inputs;
    inputs.trial = trial;
    inputs.stereopair = stereopair;
    inputs.reference_trial = reference_trial;
    
    inputs.output_path = config_.dic_path + "/" + config_.subject_id + "/" + 
        config_.material + "/" + trial + "/" + config_.phase_id;
    
    inputs.roi_file = config_.dic_path + "/" + config_.subject_id + "/" +
        config_.material + "/REF_MASK_" + reference_trial + "_" + config_.phase_id +
        "_pair" + std::to_string(stereopair) + ".mat";
    
    inputs.seed_file = config_.dic_path + "/" + config_.subject_id + "/" +
        config_.material + "/REF_SEED_" + reference_trial + "_" + config_.phase_id +
        "_pair" + std::to_string(stereopair) + ".mat";
    
    return inputs;
}

ncorr::DIC_analysis_output DIC2DWorkflow::runNcorrAnalysis(
    const cv::Mat& ref_img,
    const std::vector<cv::Mat>& cur_imgs,
    const cv::Mat& roi_mask,
    const SeedPoint& seed_point,
    const StepParameters& params,
    const std::string& output_path,
    bool parallel) {
    
    std::cout << "Running ncorr DIC analysis..." << std::endl;
    std::cout << "  Radius: " << params.radius << ", Spacing: " << params.spacing << std::endl;
    std::cout << "  Seed: (" << seed_point.pw[0] << ", " << seed_point.pw[1] << ")" << std::endl;
    
    // ncorr::Image2D expects file paths, so we need to save cv::Mat as temporary files
    std::vector<ncorr::Image2D> ncorr_images;
    std::vector<std::string> temp_image_paths;
    
    // Create temporary directory for images
    std::filesystem::path output_dir = std::filesystem::path(output_path).parent_path();
    std::string temp_dir = output_dir.string() + "/tmp_ncorr_images";
    std::filesystem::create_directories(temp_dir);
    
    // Save reference image
    std::string ref_path = temp_dir + "/ref.png";
    cv::imwrite(ref_path, ref_img);
    ncorr_images.emplace_back(ref_path);
    temp_image_paths.push_back(ref_path);
    
    // Save current images
    for (size_t i = 0; i < cur_imgs.size(); ++i) {
        std::ostringstream oss;
        oss << temp_dir << "/cur_" << std::setw(4) << std::setfill('0') << i << ".png";
        std::string cur_path = oss.str();
        cv::imwrite(cur_path, cur_imgs[i]);
        ncorr_images.emplace_back(cur_path);
        temp_image_paths.push_back(cur_path);
    }
    
    // Convert ROI mask to ncorr ROI2D
    ncorr::ROI2D roi = ROIManager::matToNcorrROI(roi_mask);
    
    // Setup DIC input
    // Note: scalefactor = spacing + 1 (this is how ncorr downsamples the displacement field)
    int scalefactor = params.spacing + 1;
    
    ncorr::DIC_analysis_input dic_input(
        ncorr_images,
        roi,
        scalefactor,
        ncorr::INTERP::QUINTIC_BSPLINE_PRECOMPUTE,
        ncorr::SUBREGION::CIRCLE,
        params.radius,
        params.total_threads,
        ncorr::DIC_analysis_config::KEEP_MOST_POINTS,
        config_.debug_mode
    );
    
    // Run DIC analysis
    ncorr::DIC_analysis_output dic_output_raw;
    
    if (parallel) {
        std::cout << "  Using parallel DIC processing..." << std::endl;
        
        // Create seed parameters from the seed point
        std::vector<ncorr::SeedParams> seeds;
        seeds.push_back(ncorr::SeedParams(seed_point.pw[0], seed_point.pw[1]));
        
        // Create parallel input structure
        ncorr::DIC_analysis_parallel_input dic_parallel_input(dic_input, seeds);
        
        // Run parallel DIC analysis
        dic_output_raw = ncorr::DIC_analysis_parallel(dic_parallel_input);
    } else {
        std::cout << "  Using auto DIC processing..." << std::endl;
        dic_output_raw = ncorr::DIC_analysis(dic_input);
    }
    
    // Post-process with correlation filtering
    //double correlation_cutoff = 0.3;
    //ncorr::DIC_analysis_output dic_filtered = ncorr::filter_by_correlation(dic_output_raw, correlation_cutoff);
    
    // Apply units
    ncorr::DIC_analysis_output dic_output = ncorr::set_units(dic_output_raw, "mm", config_.units_per_pixel);
    
    // Save to cache directory
    std::filesystem::path cache_dir = output_dir / ".cache";
    std::filesystem::create_directories(cache_dir);
    std::string cache_bin = (cache_dir / std::filesystem::path(output_path).filename()).string() + ".bin";
    save(dic_output, cache_bin);
    std::cout << "DIC analysis saved to cache: " << cache_bin << std::endl;
    
    return dic_output;
}

} // namespace cppxdic
