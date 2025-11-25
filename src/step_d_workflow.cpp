/**
 * Step D: 2D DIC Analysis Workflow for CPPXDIC
 * Implementation of complete stepD_2DDIC workflow
 * Based on MATLAB xDIC stepD_2DDIC.m
 */

#include "step_d_workflow.h"
#include "mat_writer.h"
#include "delaunay_triangulation.h"
#include "utils.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <matio.h>
#include <cmath>

namespace cppxdic {

StepDWorkflow::StepDWorkflow(const Config& config) : config_(config) {
    // Initialize with default DIC constants
    setupStepParameters();
}

std::tuple<std::string, std::vector<int>, bool> 
StepDWorkflow::execute(const std::string& trial, int stereopair) {
    std::cout << "-------------------------------------------" << std::endl;
    std::cout << "-------------------------------------------" << std::endl;
    std::cout << "Digital Image Correlation analysis launch" << std::endl;
    
    // CHECKPOINT SYSTEM (matching MATLAB implementation):
    // The following checkpoint files are checked/created to avoid redundant computation:
    // 1. REF_MASK_{reftrial}_{phase}_pair{stereopair}.mat - ROI mask (loaded from base path)
    // 2. REF_SEED_{reftrial}_{phase}_pair{stereopair}.mat - Seed point (loaded from base path)
    // 3. MATCHING2{reftrial}_pair{stereopair}.mat - Camera matching results (in output path)
    // 4. ncorr{cam1}_{cam2}.mat - Camera 1 and Camera 2 first frame matching results (in output path)
    // 5. ncorr{cam1}.mat - Camera 1 tracking results (in output path)
    // 6. ncorr{cam2}.mat - Camera 2 tracking results (in output path)
    // 7. dic_info_data_target_pair{stereopair}.mat - Trial metadata (in output path)
    
    // I. Preps analysis
    // 1. Load protocol and determine reference trial
    if (!loadProtocol()) {
        std::cerr << "Failed to load protocol" << std::endl;
        return {"", {}, false};
    }
    
    std::string reftrial = determineReferenceTrial(trial);
    std::cout << "Reference trial: " << reftrial << std::endl;
    
    // 2. Setup parameters
    setupBaseParameters(trial, stereopair, reftrial);
    
    // 3. Import video frames
    std::vector<cv::Mat> cam_first_raw, cam_second_raw;
    std::cout << "Reading video data..." << std::endl;
    if (!importVideoFrames(trial, stereopair, cam_first_raw, cam_second_raw)) {
        std::cerr << "Failed to import video frames" << std::endl;
        return {"", {}, false};
    }
    std::cout << "Reading done. Frames: " << cam_first_raw.size() << std::endl;
    
    // 4. Phase-specific frame selection
    if (config_.phase_id == "slide1") {
        size_t keep = cam_first_raw.size() / 2 + 5;
        cam_first_raw.resize(keep);
        cam_second_raw.resize(keep);
        std::cout << "Phase 'slide1': keeping " << keep << " frames" << std::endl;
    }
    
    // 5. Saturation
    std::vector<cv::Mat> cam_first_satur, cam_second_satur;
    std::cout << "Applying saturation..." << std::endl;
    performSaturation(cam_first_raw, cam_second_raw, cam_first_satur, cam_second_satur);
    
    // II. ROI, Seed, and Matching REF to Trial at frame 1
    cv::Mat refmask_REF, refmask_trial;
    SeedPoint ref_seed_point, initial_seed_point_set1;
    std::cout << "Initializing ROI and seed..." << std::endl;
    if (!initializeROIAndSeed(cam_first_satur, refmask_REF, refmask_trial,
                             ref_seed_point, initial_seed_point_set1)) {
        std::cerr << "Failed to initialize ROI and seed" << std::endl;
        return {"", {}, false};
    }
    std::cout << "--> STEP: ROI loaded and formatted" << std::endl;
    std::cout << "--> STEP: SEED loaded and formatted" << std::endl;
    std::cout << "--> STEP: Matching REF to Trial loaded and formatted" << std::endl;
    
    // III. Matching inside a Trial between cameras (cam1 -> cam2 at frame 1)
    cv::Mat refmask_trial_matched;
    SeedPoint initial_seed_point_set2;
    std::cout << "Performing camera matching..." << std::endl;
    if (!performMatching(cam_first_satur, cam_second_satur, refmask_trial,
                        initial_seed_point_set1, refmask_trial_matched,
                        initial_seed_point_set2)) {
        std::cerr << "Failed matching step" << std::endl;
        return {"", {}, false};
    }
    
    // post-III. Image filtering
    std::vector<cv::Mat> cam_first, cam_second;
    std::cout << "Applying image filtering..." << std::endl;
    applyImageFiltering(cam_first_satur, cam_second_satur, refmask_trial,
                       cam_first, cam_second);
    std::cout << "--> STEP: filtering done" << std::endl;
    
    // IV. Save trial information
    saveTrialInfo(trial, stereopair, cam_first.size());
    
    // V. Tracking camera 1
    std::cout << "Performing tracking camera 1..." << std::endl;
    if (!performTracking1(cam_first, refmask_trial, initial_seed_point_set1)) {
        std::cerr << "Failed tracking1 step" << std::endl;
        return {"", {}, false};
    }
    
    // VI. Tracking camera 2
    std::cout << "Performing tracking camera 2..." << std::endl;
    if (!performTracking2(cam_second, refmask_trial_matched, initial_seed_point_set2)) {
        std::cerr << "Failed tracking2 step" << std::endl;
        return {"", {}, false};
    }
    
    // Post-preps. Format output
    formatOutput(trial, stereopair);
    
    std::cout << "--> STEP: Ncorr analysis completed" << std::endl;
    
    // Determine pair order
    std::vector<int> pairOrder;
    bool pairForced;
    unsigned int trial_idx = std::stoi(trial);
    if (trial_idx > 0 && trial_idx < protocol_info_.dircond.size()) {
        protocol_info_.getPairOrder(protocol_info_.dircond[trial_idx], pairOrder, pairForced);
    } else {
        pairOrder = {1, 2};
        pairForced = false;
    }
    
    return {base_params_.outputPath, pairOrder, pairForced};
}

void StepDWorkflow::setupBaseParameters(const std::string& trial,
                                       int stereopair,
                                       const std::string& reftrial) {
    base_params_.baseDataPath = config_.data_path;
    base_params_.baseResultPath = config_.dic_path;
    base_params_.subject = config_.subject_id;
    base_params_.material = config_.material;
    base_params_.trial = trial;
    base_params_.stereopair = stereopair;
    base_params_.phase = config_.phase_id;
    base_params_.reftrial = reftrial;
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
    getCameraNumbers(stereopair, base_params_.cam_1, base_params_.cam_2);
    
    // Set output path
    base_params_.outputPath = base_params_.baseResultPath + "/" + 
        base_params_.subject + "/" + base_params_.material + "/" +
        base_params_.trial + "/" + base_params_.phase;
    
    // Create output directory
    std::filesystem::create_directories(base_params_.outputPath);
    
    // Set file paths
    base_params_.roifile = base_params_.baseResultPath + "/" + base_params_.subject + "/" +
        base_params_.material + "/REF_MASK_" + reftrial + "_" + base_params_.phase +
        "_pair" + std::to_string(stereopair) + ".mat";
    
    base_params_.matchingfile = base_params_.outputPath + "/MATCHING2" + 
        reftrial + "_pair" + std::to_string(stereopair) + ".mat";
    
    base_params_.seedfile = base_params_.baseResultPath + "/" + base_params_.subject + "/" +
        base_params_.material + "/REF_SEED_" + reftrial + "_" + base_params_.phase +
        "_pair" + std::to_string(stereopair) + ".mat";
}

void StepDWorkflow::setupStepParameters() {
    // Setup tracking parameters (camera 1)
    step1_params_.type = "regular";
    step1_params_.radius = DICConstants::SUBSET_RADIUS_TRACKING;
    step1_params_.spacing = DICConstants::SUBSET_SPACING;
    step1_params_.cutoff_diffnorm = DICConstants::CUTOFF_TRACKING;
    step1_params_.cutoff_iteration = DICConstants::NUMBER_ITERATION_SOLVER;
    step1_params_.total_threads = DICConstants::NUMBER_THREADS;
    step1_params_.stepanalysis_params.enabled = true;
    step1_params_.stepanalysis_params.type = "seed";
    step1_params_.stepanalysis_params.auto_update = true;
    step1_params_.stepanalysis_params.step = 10;
    
    // Setup tracking parameters (camera 2) - same as camera 1
    step2_params_ = step1_params_;
    
    // Setup matching parameters (camera 1 -> camera 2)
    step1_2_params_ = step1_params_;
    step1_2_params_.radius = DICConstants::SUBSET_RADIUS_MATCHING;  // Larger radius for matching
    step1_2_params_.cutoff_diffnorm = DICConstants::CUTOFF_MATCHING;
}

bool StepDWorkflow::loadProtocol() {
    // Load protocol MAT file
    std::string protocol_path = config_.data_path + "/rawdata/" +
        config_.subject_id + "/speckles/" + config_.material + "/protocol/";
    std::cout << "protocol_path: " << protocol_path << std::endl;
    
    // Find MAT file in protocol directory
    std::vector<std::string> mat_files;
    if (std::filesystem::exists(protocol_path)) {
        for (const auto& entry : std::filesystem::directory_iterator(protocol_path)) {
            if (entry.path().extension() == ".mat") {
                mat_files.push_back(entry.path().string());
            }
        }
    }
    
    if (mat_files.empty()) {
        std::cerr << "No protocol file found in: " << protocol_path << std::endl;
        return false;
    }
    
    // Load first MAT file found
    std::string protocol_file = mat_files[0];
    std::cout << "Loading protocol: " << protocol_file << std::endl;
    
    mat_t* matfp = Mat_Open(protocol_file.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open protocol file" << std::endl;
        return false;
    }
    
    // Read 'cond' struct
    matvar_t* cond = Mat_VarRead(matfp, "cond");
    if (!cond || cond->class_type != MAT_C_STRUCT) {
        if (cond) Mat_VarFree(cond);
        Mat_Close(matfp);
        std::cerr << "Variable 'cond' not found or not a struct" << std::endl;
        return false;
    }
    
    // Extract protocol data from cond struct
    // For now, use dummy data as complete struct parsing is complex
    // TODO: Complete implementation when detailed protocol info is needed
    protocol_info_.dircond = {"", "Ubnf", "Rbnf", "Ubnf", "Rbnf", "Ubnf"};
    protocol_info_.nfcond = {0, 1, 1, 5, 5, 1};
    protocol_info_.spdcond = {0, 1, 1, 1, 1, 1};
    protocol_info_.repcond = {0, 1, 1, 1, 1, 1};
    
    if (cond) Mat_VarFree(cond);
    Mat_Close(matfp);
    
    std::cout << "Warning: Using dummy protocol data" << std::endl;
    return true;
}

std::string StepDWorkflow::determineReferenceTrial(const std::string& trial) {
    // Use config reference trial if set
    if (config_.ref_trial_id > 0) {
        std::ostringstream oss;
        oss << std::setw(3) << std::setfill('0') << config_.ref_trial_id;
        return oss.str();
    }

    if (config_.debug_mode) {
        std::cout << "Reference trial not set, using current trial: " << trial << std::endl;
    }
    
    return trial;  // Fallback to current trial
}

bool StepDWorkflow::importVideoFrames(const std::string& trial,
                                     int stereopair,
                                     std::vector<cv::Mat>& cam_first_raw,
                                     std::vector<cv::Mat>& cam_second_raw) {
    // Use Utils::importVid to get frame paths
    std::vector<std::string> cam1_frames, cam2_frames;
    
    // Convert trial string to integer for Utils::importVid
    int trial_num = std::stoi(trial);
    
    // Import video frames using Utils
    if (!Utils::importVid(config_, trial_num, stereopair, cam1_frames, cam2_frames)) {
        std::cerr << "Failed to import video frames for trial " << trial 
                  << " stereopair " << stereopair << std::endl;
        return false;
    }
    
    // Check that we got frames
    if (cam1_frames.empty() || cam2_frames.empty()) {
        std::cerr << "No frames imported for trial " << trial 
                  << " stereopair " << stereopair << std::endl;
        return false;
    }
    
    // Check frame count consistency
    if (cam1_frames.size() != cam2_frames.size()) {
        std::cerr << "Frame count mismatch: cam1=" << cam1_frames.size() 
                  << " cam2=" << cam2_frames.size() << std::endl;
        return false;
    }
    
    // Load frames into cv::Mat vectors
    cam_first_raw.clear();
    cam_second_raw.clear();
    cam_first_raw.reserve(cam1_frames.size());
    cam_second_raw.reserve(cam2_frames.size());
    
    for (size_t i = 0; i < cam1_frames.size(); ++i) {
        // Load first camera frame
        cv::Mat img1 = cv::imread(cam1_frames[i], cv::IMREAD_GRAYSCALE);
        if (img1.empty()) {
            std::cerr << "Failed to load frame: " << cam1_frames[i] << std::endl;
            return false;
        }
        
        // Load second camera frame
        cv::Mat img2 = cv::imread(cam2_frames[i], cv::IMREAD_GRAYSCALE);
        if (img2.empty()) {
            std::cerr << "Failed to load frame: " << cam2_frames[i] << std::endl;
            return false;
        }
        
        cam_first_raw.push_back(img1);
        cam_second_raw.push_back(img2);
    }
    
    std::cout << "Loaded " << cam_first_raw.size() << " frames for each camera" << std::endl;
    return true;
}

void StepDWorkflow::performSaturation(const std::vector<cv::Mat>& cam_first_raw,
                                     const std::vector<cv::Mat>& cam_second_raw,
                                     std::vector<cv::Mat>& cam_first_satur,
                                     std::vector<cv::Mat>& cam_second_satur) {
    cam_first_satur = ImageProcessor::saturate(cam_first_raw, base_params_.limit_grayscale);
    cam_second_satur = ImageProcessor::saturate(cam_second_raw, base_params_.limit_grayscale);
}

bool StepDWorkflow::initializeROIAndSeed(const std::vector<cv::Mat>& cam_first_satur,
                                        cv::Mat& refmask_REF,
                                        cv::Mat& refmask_trial,
                                        SeedPoint& ref_seed_point,
                                        SeedPoint& initial_seed_point_set1) {
    // Load or create ROI
    refmask_REF = ROIManager::loadOrCreateROI(base_params_, cam_first_satur[0]);
    if (refmask_REF.empty()) {
        return false;
    }
    
    // Load or create seed
    ref_seed_point = ROIManager::loadOrCreateSeed(base_params_, refmask_trial);
    
    // If matching file exists, load it to get transformed ROI and seed
    if (std::filesystem::exists(base_params_.matchingfile)) {
        cv::Mat U_mapped, V_mapped;
        if (ROIManager::loadMatchingResults(base_params_.matchingfile,
                                           refmask_REF, refmask_trial,
                                           U_mapped, V_mapped,
                                           step1_params_.spacing)) {
            // Map seed point using displacement fields
            ref_seed_point.sw = ROIManager::mapPixel2Subset(ref_seed_point.pw,
                                                           step1_params_.spacing);
            initial_seed_point_set1.sw = ROIManager::mapPointCoordinate(ref_seed_point.sw,
                                                                        U_mapped, V_mapped);
            initial_seed_point_set1.pw = ROIManager::mapSubset2Pixel(initial_seed_point_set1.sw,
                                                                     step1_params_.spacing);
            return true;
        }
    }
    
    // No matching file - use seed as-is
    std::vector<cv::Mat> reftrial_cam_first_raw, reftrial_cam_second_raw;
    std::cout << "Reading REF Trial video data..." << std::endl;
    if (!importVideoFrames(base_params_.reftrial, 
                           base_params_.stereopair, 
                           reftrial_cam_first_raw, 
                           reftrial_cam_second_raw)) {
        std::cerr << "Failed to import REF Trial video frames" << std::endl;
        return false;
    }
    std::cout << "Reading REF Trial video data done. Frames: " << reftrial_cam_first_raw.size() << std::endl;
    
    std::ostringstream message_oss;
    message_oss << "Matching Pair " << base_params_.stereopair << ": trial " << base_params_.reftrial << "'s frame 1 TO trial " << base_params_.trial << "'s frame 1";
    return matchingInitialFrame(reftrial_cam_first_raw, 
                                cam_first_satur, 
                                refmask_REF,
                                base_params_.matchingfile,
                                message_oss.str(), 
                                initial_seed_point_set1, 
                                refmask_trial, 
                                initial_seed_point_set1);
}

bool StepDWorkflow::matchingInitialFrame(const std::vector<cv::Mat>& cam_ref,
                                   const std::vector<cv::Mat>& cam_cur,
                                   const cv::Mat& refmask_ref,
                                   const std::string ncorr_matching_path,
                                   const std::string message,
                                   const SeedPoint& initial_seed_point_ref,
                                   cv::Mat& refmask_cur_matched,
                                   SeedPoint& initial_seed_point_cur) {
    std::cout << message << std::endl;
    
    // Prepare images: ref = cam_ref[0], cur = [cam_cur[0]]
    std::vector<cv::Mat> cur_imgs = {cam_cur[0]};
    
    // Set seed for matching
    step1_2_params_.initial_seed = {
        static_cast<int>(initial_seed_point_ref.pw[0]),
        static_cast<int>(initial_seed_point_ref.pw[1])
    };
    
    // Run ncorr analysis and save to cache-compatible path (for formatOutput to find)
    auto dic_output = runNcorrAnalysis(cam_ref[0], cur_imgs, refmask_ref,
                                      initial_seed_point_ref, step1_2_params_,
                                      ncorr_matching_path, false); // false for parallel processing
    
    // Extract matched ROI and displacement fields
    if (!dic_output.disps.empty()) {
        // NOTE: We use the SAME refmask_trial for both cameras since they image the same region
        // The matching step transforms the seed point coordinates, not the ROI itself
        // dic_output.disps[0].get_roi() is the sparse displacement grid, not the full image ROI
        refmask_cur_matched = refmask_ref.clone();
        
        // Extract displacement fields to map seed point
        const auto& disp = dic_output.disps[0];
        const auto& u_data = disp.get_u();
        const auto& v_data = disp.get_v();
        
        // Get the underlying Array2D from Data2D
        const auto& u_array = u_data.get_array();
        const auto& v_array = v_data.get_array();
        
        // Convert ncorr Array2D to cv::Mat for displacement mapping
        cv::Mat U_mapped(u_data.data_height(), u_data.data_width(), CV_64F);
        cv::Mat V_mapped(v_data.data_height(), v_data.data_width(), CV_64F);
        
        for (size_t y = 0; y < static_cast<size_t>(u_data.data_height()); ++y) {
            for (size_t x = 0; x < static_cast<size_t>(u_data.data_width()); ++x) {
                U_mapped.at<double>(y, x) = u_array(y, x);
                V_mapped.at<double>(y, x) = v_array(y, x);
            }
        }
        
        // Map seed point using displacement fields
        initial_seed_point_cur.sw = ROIManager::mapPointCoordinate(
            initial_seed_point_ref.sw, U_mapped, V_mapped);
        initial_seed_point_cur.pw = ROIManager::mapSubset2Pixel(
            initial_seed_point_cur.sw, step1_2_params_.spacing);
    } else {
        std::cerr << "No displacement output from matching" << std::endl;
        return false;
    }
    
    std::cout << "--> STEP: " << message << " done" << std::endl;
    return true;
}

bool StepDWorkflow::performMatching(const std::vector<cv::Mat>& cam_first_satur,
                                   const std::vector<cv::Mat>& cam_second_satur,
                                   const cv::Mat& refmask_trial,
                                   const SeedPoint& initial_seed_point_set1,
                                   cv::Mat& refmask_trial_matched,
                                   SeedPoint& initial_seed_point_set2) {
    std::cout << "MATCHING STEP - RUN #1" << std::endl;
    
    // Get camera numbers
    int cam_1, cam_2;
    getCameraNumbers(base_params_.stereopair, cam_1, cam_2);
    
    // Build cache-compatible filename (e.g., ncorr12.mat for cameras 1,2)
    std::string ncorr_matching_path = base_params_.outputPath + "/ncorr" + 
        std::to_string(cam_1) + std::to_string(cam_2) + ".mat";
    
    // Check cache to avoid redundant computation
    std::filesystem::path cache_bin = std::filesystem::path(ncorr_matching_path).parent_path() / 
        ".cache" / ("ncorr" + std::to_string(cam_1) + std::to_string(cam_2) + ".mat.bin");
    
    if (std::filesystem::exists(cache_bin)) {
        std::cout << "Checkpoint found: \"" << cache_bin.string() << "\"" << std::endl;
        std::cout << "--> STEP: Ncorr matching 1-2 loaded from checkpoint (skipped computation)" << std::endl;
        
        // Still need to load the displacement fields to compute seed mapping
        auto dic_output = ncorr::DIC_analysis_output::load(cache_bin.string());
        
        if (!dic_output.disps.empty()) {
            refmask_trial_matched = refmask_trial.clone();
            
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
            
            initial_seed_point_set2.sw = ROIManager::mapPointCoordinate(
                initial_seed_point_set1.sw, U_mapped, V_mapped);
            initial_seed_point_set2.pw = ROIManager::mapSubset2Pixel(
                initial_seed_point_set2.sw, step1_2_params_.spacing);
            
            std::cout << "--> STEP: Ncorr matching 1-2 done" << std::endl;
            return true;
        } else {
            std::cerr << "Warning: Cached matching file has no displacements, re-running..." << std::endl;
        }
    }
    
    return matchingInitialFrame(cam_first_satur, 
                                cam_second_satur, 
                                refmask_trial, 
                                ncorr_matching_path, 
                                "Matching 1-2", 
                                initial_seed_point_set1, 
                                refmask_trial_matched, 
                                initial_seed_point_set2);
}

bool StepDWorkflow::performTracking(const int tracking_number,
                         const std::vector<cv::Mat>& cam_frames,
                         const cv::Mat& refmask,
                         const SeedPoint& initial_seed_point) {
    // Get actual camera number for this pair
    int cam_1, cam_2;
    getCameraNumbers(base_params_.stereopair, cam_1, cam_2);

    auto cam_number = tracking_number == 1 ? cam_1 : cam_2;

    std::cout << "TRACKING STEP " << tracking_number << std::endl;
    
    std::string output_path = base_params_.outputPath + "/ncorr" + std::to_string(cam_number) + ".mat";
    
    // Checkpoint: Check if cached .bin file already exists
    std::filesystem::path cache_bin = std::filesystem::path(output_path).parent_path() / ".cache" / ("ncorr" + std::to_string(cam_number) + ".mat.bin");
    if (std::filesystem::exists(cache_bin)) {
        std::cout << "Checkpoint found: " << cache_bin << std::endl;
        std::cout << "--> STEP: Ncorr " << cam_number << " loaded from checkpoint (skipped computation)" << std::endl;
        return true;
    }
    
    auto step_params_ = step2_params_;
    if (tracking_number == 1) {
        step_params_ = step1_params_;
        step1_params_.initial_seed = {
            static_cast<int>(initial_seed_point.pw[0]),
            static_cast<int>(initial_seed_point.pw[1])
        };
    } else {
        step2_params_.initial_seed = {
            static_cast<int>(initial_seed_point.pw[0]),
            static_cast<int>(initial_seed_point.pw[1])
        };
    }
    
    auto dic_output = runNcorrAnalysis(cam_frames[0], cam_frames, refmask,
                                      initial_seed_point, step_params_,
                                      output_path, config_.parallel_processing);
    
    std::cout << "--> STEP: Ncorr " << cam_number << " done and saved to " << output_path << std::endl;
    return true;
}

bool StepDWorkflow::performTracking1(const std::vector<cv::Mat>& cam_first,
                                    const cv::Mat& refmask_trial,
                                    const SeedPoint& initial_seed_point_set1) {
    return performTracking(1, cam_first, refmask_trial, initial_seed_point_set1);
}

bool StepDWorkflow::performTracking2(const std::vector<cv::Mat>& cam_second,
                                    const cv::Mat& refmask_trial_matched,
                                    const SeedPoint& initial_seed_point_set2) {
    return performTracking(2, cam_second, refmask_trial_matched, initial_seed_point_set2);
}

void StepDWorkflow::applyImageFiltering(const std::vector<cv::Mat>& cam_first_satur,
                                       const std::vector<cv::Mat>& cam_second_satur,
                                       const cv::Mat& refmask_trial,
                                       std::vector<cv::Mat>& cam_first,
                                       std::vector<cv::Mat>& cam_second) {
    // Apply Ben's filtering
    std::vector<int> param_filt = {25, 300};
    
    auto [filtered_first, gs_bounds] = ImageProcessor::filterLikeBen(
        cam_first_satur, refmask_trial, param_filt, nullptr);
    
    // Use same boundaries for second camera
    auto [filtered_second, _] = ImageProcessor::filterLikeBen(
        cam_second_satur, refmask_trial, param_filt, &gs_bounds);
    
    cam_first = filtered_first;
    cam_second = filtered_second;
}

void StepDWorkflow::saveTrialInfo(const std::string& trial, int stereopair, int num_frames) {
    std::string filename = base_params_.outputPath + "/dic_info_data_target_pair" +
        std::to_string(stereopair) + ".mat";
    
    double actual_fps = DICConstants::TRUE_FPS / static_cast<double>(base_params_.jump);
    
    std::vector<int> idxframe;
    for (int i = base_params_.idxstart_set; 
         i <= base_params_.idxend_set && i < num_frames; 
         i += base_params_.jump) {
        idxframe.push_back(i);
    }
    
    MatWriter::writeTrialInfoFile(filename, actual_fps, idxframe);
}

void StepDWorkflow::formatOutput(const std::string& trial, int stereopair) {
    std::cout << "Formatting output files (step2_dic_finish equivalent)..." << std::endl;
    
    if (!config_.generate_mat_files) {
        std::cout << "Skipping formatOutput (generate_mat_files=false)" << std::endl;
        return;
    }
    
    int cam_1, cam_2;
    getCameraNumbers(stereopair, cam_1, cam_2);
    
    std::filesystem::path cache_dir = std::filesystem::path(base_params_.outputPath) / ".cache";
    // Use actual camera numbers (e.g., ncorr1.mat.bin, ncorr2.mat.bin for pair 1; ncorr3.mat.bin, ncorr4.mat.bin for pair 2)
    std::string ncorr1_bin = (cache_dir / ("ncorr" + std::to_string(cam_1) + ".mat.bin")).string();
    std::string ncorr2_bin = (cache_dir / ("ncorr" + std::to_string(cam_2) + ".mat.bin")).string();
    // For stereo pair 1: cameras 1,2 -> ncorr12.mat.bin (no underscore to match save format)
    // For stereo pair 2: cameras 3,4 -> ncorr34.mat.bin
    std::string ncorr12_bin = (cache_dir / ("ncorr" + std::to_string(cam_1) + std::to_string(cam_2) + ".mat.bin")).string();
    
    if (!std::filesystem::exists(ncorr1_bin) || !std::filesystem::exists(ncorr2_bin) || !std::filesystem::exists(ncorr12_bin)) {
        std::cerr << "Warning: cached ncorr result files not found" << std::endl;
        if (!std::filesystem::exists(ncorr1_bin)) std::cerr << "  Missing: " << ncorr1_bin << std::endl;
        if (!std::filesystem::exists(ncorr2_bin)) std::cerr << "  Missing: " << ncorr2_bin << std::endl;
        if (!std::filesystem::exists(ncorr12_bin)) std::cerr << "  Missing: " << ncorr12_bin << std::endl;
        return;
    }
    
    std::string output_file = base_params_.outputPath + "/myDIC2DpairResults_C_" + 
        std::to_string(cam_1) + "_C_" + std::to_string(cam_2) + ".mat";
    
    if (std::filesystem::exists(output_file)) {
        std::cout << "Checkpoint found: " << output_file << std::endl;
        return;
    }
    
    std::cout << "  Loading cached DIC outputs..." << std::endl;
    ncorr::DIC_analysis_output dic1 = ncorr::DIC_analysis_output::load(ncorr1_bin);
    ncorr::DIC_analysis_output dic2 = ncorr::DIC_analysis_output::load(ncorr2_bin);
    
    if (dic1.disps.empty() || dic2.disps.empty()) {
        std::cerr << "Error: DIC outputs are empty" << std::endl;
        return;
    }
    
    int n_frames = dic1.disps.size();
    int Factor = dic1.disps[0].get_scalefactor() + 1;
    std::cout << "  Processing " << n_frames << " frames, Factor=" << Factor << std::endl;
    
    DIC2DPairResults results;
    results.nCamRef = cam_1;
    results.nCamDef = cam_2;
    results.nImages = n_frames;
    
    const auto& roi1 = dic1.disps[0].get_roi();
    const auto& roi_mask = roi1.get_mask();
    results.ROImask = cv::Mat(roi_mask.height(), roi_mask.width(), CV_8U);
    for (int y = 0; y < roi_mask.height(); ++y) {
        for (int x = 0; x < roi_mask.width(); ++x) {
            results.ROImask.at<uint8_t>(y, x) = roi_mask(y, x) ? 255 : 0;
        }
    }
    
    std::vector<cv::Point2f> Pref;
    for (int y = 0; y < roi_mask.height(); ++y) {
        for (int x = 0; x < roi_mask.width(); ++x) {
            if (roi_mask(y, x)) {
                Pref.push_back(cv::Point2f(x * Factor, y * Factor));
            }
        }
    }
    std::cout << "  Reference points: " << Pref.size() << std::endl;
    
    results.Points.resize(n_frames * 2);
    results.CorCoeffVec.resize(n_frames * 2);
    
    std::cout << "  Processing cam1 frames..." << std::endl;
    for (int ii = 0; ii < n_frames; ++ii) {
        const auto& disp = dic1.disps[ii];
        const auto& u_array = disp.get_u().get_array();
        const auto& v_array = disp.get_v().get_array();
        
        std::vector<cv::Point2f> points;
        std::vector<double> corrcoef;
        points.reserve(Pref.size());
        corrcoef.reserve(Pref.size());
        
        int idx = 0;
        for (int y = 0; y < roi_mask.height(); ++y) {
            for (int x = 0; x < roi_mask.width(); ++x) {
                if (roi_mask(y, x)) {
                    double u = u_array(y, x);
                    double v = v_array(y, x);
                    if (u == 0.0 && v == 0.0) {
                        points.push_back(cv::Point2f(NAN, NAN));
                        corrcoef.push_back(NAN);
                    } else {
                        points.push_back(cv::Point2f(Pref[idx].x + u, Pref[idx].y + v));
                        corrcoef.push_back(0.95);
                    }
                    idx++;
                }
            }
        }
        // Convert to Points2D structure
        Points2D pts2d;
        pts2d.x.reserve(points.size());
        pts2d.y.reserve(points.size());
        for (const auto& p : points) {
            pts2d.x.push_back(static_cast<double>(p.x));
            pts2d.y.push_back(static_cast<double>(p.y));
        }
        results.Points[ii] = std::move(pts2d);
        // Store average corrcoef as a scalar per frame
        double avg_corr = 0.0;
        if (!corrcoef.empty()) {
            double sum = 0.0; int cnt = 0;
            for (double c : corrcoef) { if (!std::isnan(c)) { sum += c; ++cnt; } }
            avg_corr = (cnt>0) ? (sum / static_cast<double>(cnt)) : std::numeric_limits<double>::quiet_NaN();
        } else {
            avg_corr = std::numeric_limits<double>::quiet_NaN();
        }
        results.CorCoeffVec[ii] = avg_corr;
    }
    
    std::cout << "  Processing cam2 frames..." << std::endl;
    for (int ii = 0; ii < n_frames; ++ii) {
        const auto& disp2 = dic2.disps[ii];
        const auto& u2_array = disp2.get_u().get_array();
        const auto& v2_array = disp2.get_v().get_array();
        
        std::vector<cv::Point2f> points;
        std::vector<double> corrcoef;
        
        int idx = 0;
        for (int y = 0; y < roi_mask.height(); ++y) {
            for (int x = 0; x < roi_mask.width(); ++x) {
                if (roi_mask(y, x)) {
                    double u = u2_array(y, x);
                    double v = v2_array(y, x);
                    points.push_back(cv::Point2f(Pref[idx].x + u, Pref[idx].y + v));
                    corrcoef.push_back(0.95);
                    idx++;
                }
            }
        }
        Points2D pts2d2;
        pts2d2.x.reserve(points.size());
        pts2d2.y.reserve(points.size());
        for (const auto& p : points) {
            pts2d2.x.push_back(static_cast<double>(p.x));
            pts2d2.y.push_back(static_cast<double>(p.y));
        }
        results.Points[n_frames + ii] = std::move(pts2d2);
        double avg_corr2 = 0.0;
        if (!corrcoef.empty()) {
            double sum2 = 0.0; int cnt2 = 0;
            for (double c : corrcoef) { if (!std::isnan(c)) { sum2 += c; ++cnt2; } }
            avg_corr2 = (cnt2>0) ? (sum2 / static_cast<double>(cnt2)) : std::numeric_limits<double>::quiet_NaN();
        } else {
            avg_corr2 = std::numeric_limits<double>::quiet_NaN();
        }
        results.CorCoeffVec[n_frames + ii] = avg_corr2;
    }
    
    std::cout << "  Creating Delaunay triangulation..." << std::endl;
    results.Faces = DelaunayTriangulation::compute(Pref);
    double max_edge = 1.1 * std::sqrt(2.0) * Factor;
    results.Faces = DelaunayTriangulation::filterByEdgeLength(results.Faces, Pref, max_edge);
    results.Faces = DelaunayTriangulation::flipOrientation(results.Faces);
    std::cout << "  Triangles: " << (results.Faces.size() / 3) << std::endl;
    
    results.FaceColors.resize(results.Faces.size() / 3, 128.0);
    
    std::cout << "  Writing results..." << std::endl;
    bool success = MatWriter::writeDIC2DPairResults(output_file, results);
    if (success) {
        std::cout << "Output formatting complete: " << output_file << std::endl;
    } else {
        std::cerr << "Failed to write DIC2DPairResults" << std::endl;
    }
}

ncorr::DIC_analysis_output StepDWorkflow::runNcorrAnalysis(
    const cv::Mat& ref_img,
    const std::vector<cv::Mat>& cur_imgs,
    const cv::Mat& roi_mask,
    const SeedPoint& seed_point,
    const StepParameters& step_params,
    const std::string& output_path,
    const bool go_parallel) {
    
    std::cout << "Running ncorr DIC analysis..." << std::endl;
    std::cout << "  Radius: " << step_params.radius << ", Spacing: " << step_params.spacing << std::endl;
    std::cout << "  Scalefactor: " << (step_params.spacing + 1) << " (spacing + 1)" << std::endl;
    std::cout << "  Seed: (" << seed_point.pw[0] << ", " << seed_point.pw[1] << ")" << std::endl;
    
    // Convert images to ncorr Image2D format
    // ncorr::Image2D expects file paths, so we need to save cv::Mat as temporary files
    std::vector<ncorr::Image2D> ncorr_images;
    std::vector<std::string> temp_image_paths;
    
    // Create temporary directory for images
    std::string temp_dir = base_params_.outputPath + "/tmp_ncorr_images";
    std::filesystem::create_directories(temp_dir);
    
    // Save reference image
    std::string ref_path = temp_dir + "/ref.png";
    cv::imwrite(ref_path, ref_img);
    ncorr_images.emplace_back(ref_path);
    temp_image_paths.push_back(ref_path);
    
    // Save current images
    for (unsigned int i = 0; i < cur_imgs.size(); ++i) {
        std::ostringstream oss;
        oss << temp_dir << "/cur_" << std::setw(4) << std::setfill('0') << i << ".png";
        std::string cur_path = oss.str();
        cv::imwrite(cur_path, cur_imgs[i]);
        ncorr_images.emplace_back(cur_path);
        temp_image_paths.push_back(cur_path);
    }
    
    // Convert ROI mask
    ncorr::ROI2D roi = ROIManager::matToNcorrROI(roi_mask);
    
    // Setup DIC input
    // Note: scalefactor = spacing + 1 (this is how ncorr downsamples the displacement field)
    int scalefactor = step_params.spacing + 1;
    
    ncorr::DIC_analysis_input dic_input(
        ncorr_images,
        roi,
        scalefactor,
        ncorr::INTERP::QUINTIC_BSPLINE_PRECOMPUTE,
        ncorr::SUBREGION::CIRCLE,
        step_params.radius,
        step_params.total_threads,
        ncorr::DIC_analysis_config::KEEP_MOST_POINTS,
        config_.debug_mode
    );
    
    // Run DIC analysis (returns Lagrangian perspective in pixels)
    ncorr::DIC_analysis_output dic_output_raw;
    
    if (go_parallel) {
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
    
    // Post-process with both perspectives
    std::cout << "Post-processing displacements..." << std::endl;
    
    // Step 1: Apply correlation filtering (optional, controlled by config)
    double correlation_cutoff = 0.3;  // TODO: make this configurable
    ncorr::DIC_analysis_output dic_filtered = ncorr::filter_by_correlation(dic_output_raw, correlation_cutoff);
    
    // Step 2: Convert to Eulerian perspective with sign inversion (still in pixels)
    ncorr::DIC_analysis_output dic_eulerian_pixels = ncorr::change_perspective_with_inversion(
        dic_filtered, 
        ncorr::INTERP::CUBIC_KEYS  // Use cubic interpolation for perspective change
    );
    
    // Step 3: Apply units to BOTH perspectives
    ncorr::DIC_analysis_output dic_lagrangian = ncorr::set_units(dic_filtered, "mm", config_.units_per_pixel);
    ncorr::DIC_analysis_output dic_eulerian = ncorr::set_units(dic_eulerian_pixels, "mm", config_.units_per_pixel);
    
    std::cout << "  Created both Lagrangian and Eulerian perspectives" << std::endl;
    
    // Strategy: Always save .bin to .cache/ subdirectory (for C++ internal use)
    //           Optionally generate .mat files (for MATLAB compatibility)
    
    // Create .cache directory if it doesn't exist
    std::filesystem::path output_dir = std::filesystem::path(output_path).parent_path();
    std::filesystem::path cache_dir = output_dir / ".cache";
    std::filesystem::create_directories(cache_dir);
    
    // Save Lagrangian .bin to cache directory (internal format for C++)
    std::string cache_bin = (cache_dir / std::filesystem::path(output_path).filename()).string() + ".bin";
    save(dic_lagrangian, cache_bin);
    std::cout << "DIC analysis saved to cache: " << cache_bin << std::endl;
    
    // Optionally generate .mat file with BOTH perspectives (for MATLAB users)
    if (config_.generate_mat_files) {
        // For MATCHING files, write with both perspectives
        if (output_path.find("MATCHING") != std::string::npos) {
            // Extract necessary data for writeMatchingFile
            cv::Mat ref_img_out = ref_img.clone();
            cv::Mat cur_img_out = cur_imgs[0].clone();  // First current image
            cv::Mat ref_roi_out = roi_mask.clone();
            cv::Mat cur_roi_out = roi_mask.clone();  // Will be updated inside writeMatchingFile
            
            // Prepare dispinfo
            std::map<std::string, double> dispinfo;
            dispinfo["radius"] = step_params.radius;
            dispinfo["spacing"] = step_params.spacing;
            dispinfo["cutoff_corrcoef"] = correlation_cutoff;
            dispinfo["units_per_pixel"] = config_.units_per_pixel;
            
            // Write with both perspectives
            bool success = MatWriter::writeMatchingFile(
                output_path,
                ref_img_out, cur_img_out,
                ref_roi_out, cur_roi_out,
                dic_lagrangian,   // _ref_formatted
                dic_eulerian,     // _cur_formatted
                dispinfo
            );
            
            if (success) {
                std::cout << "MATLAB .mat file with both perspectives generated: " << output_path << std::endl;
            } else {
                std::cerr << "Warning: Failed to generate .mat file" << std::endl;
            }
        } else {
            // For tracking files (ncorr1.mat, ncorr2.mat), use legacy method
            bool success = MatWriter::convertBinToMat(cache_bin, output_path, dic_input);
            if (success) {
                std::cout << "MATLAB .mat file generated: " << output_path << std::endl;
            } else {
                std::cerr << "Warning: Failed to generate .mat file" << std::endl;
            }
        }
        
        // Optionally cleanup cache .bin after .mat generation
        if (config_.cleanup_cache_bins) {
            std::filesystem::remove(cache_bin);
            std::cout << "Cache .bin cleaned up: " << cache_bin << std::endl;
        }
    }
    
    // Return Lagrangian output for further processing
    ncorr::DIC_analysis_output dic_output = dic_lagrangian;
    
    return dic_output;
}

void StepDWorkflow::getCameraNumbers(int stereopair, int& cam_first, int& cam_second) {
    // Standard mapping: pair 1 -> cameras 1,2; pair 2 -> cameras 3,4
    cam_first = (stereopair - 1) * 2 + 1;
    cam_second = (stereopair - 1) * 2 + 2;
}

} // namespace cppxdic
