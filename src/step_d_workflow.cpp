/**
 * Step D: 2D DIC Analysis Workflow for CPPXDIC
 * Implementation of complete stepD_2DDIC workflow
 * Based on MATLAB xDIC stepD_2DDIC.m
 */

#include "step_d_workflow.h"
#include "Array2D.h"
#include "mat_writer.h"
#include "mat_reader.h"
#include "data_serializer.h"
#include "delaunay_triangulation.h"
#include "ncorr.h"
#include "utils.h"
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <sstream>
#include <iomanip>
#include <filesystem>
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
    // 3. MATCHING2{reftrial}_pair{stereopair}.bin - REF-to-trial matching (in output path)
    // 4. ncorr{cam1}{cam2}.bin - Inter-camera matching (in output path)
    // 5. ncorr{cam1}.bin - Camera 1 tracking results (in output path)
    // 6. ncorr{cam2}.bin - Camera 2 tracking results (in output path)
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
    std::cout << "Reading video data... ";
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
    
    cv::imwrite("cam_first_satur.png", cam_first_satur[0]);
    cv::imwrite("cam_second_satur.png", cam_second_satur[0]);
    cv::imwrite("cam_first_raw.png", cam_first_raw[0]);
    cv::imwrite("cam_second_raw.png", cam_second_raw[0]);
    
    // II. ROI, Seed, and Matching REF to Trial at frame 1
    cv::Mat refmask_REF, refmask_trial;
    SeedPoint ref_seed_point, initial_seed_point_set1;
    std::cout << "Initializing ROI, seed, and matching REF to Trial..." << std::endl;
    if (!initializeROIAndSeed(cam_first_satur, refmask_REF, refmask_trial,
                             ref_seed_point, initial_seed_point_set1)) {
        std::cerr << "Failed to initialize ROI and seed" << std::endl;
        return {"", {}, false};
    }

    std::cout << "DEBUG: ref_seed_point = " << ref_seed_point.pw[0] << ", " << ref_seed_point.pw[1] << std::endl;
    std::cout << "DEBUG: initial_seed_point_set1 = " << initial_seed_point_set1.pw[0] << ", " << initial_seed_point_set1.pw[1] << std::endl;


    std::cout << "--> STEP: ROI loaded and formatted" << std::endl;
    std::cout << "--> STEP: SEED loaded and formatted" << std::endl;
    std::cout << "--> STEP: Matching REF to Trial loaded and formatted" << std::endl;


    //cv::imshow("refmask_REF", refmask_REF);
    //cv::imshow("refmask_trial", refmask_trial);
    //cv::waitKey(0);
    
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

    std::cout << "DEBUG: initial_seed_point_set2 = " << initial_seed_point_set2.pw[0] << ", " << initial_seed_point_set2.pw[1] << std::endl;

    //cv::imshow("refmask_trial_matched", refmask_trial_matched);
    //cv::waitKey(0);
    
    // post-III. Image filtering
    std::vector<cv::Mat> cam_first, cam_second;
    if(config_.im_filter_mode) {
        std::cout << "Applying image filtering..." << std::endl;
        applyImageFiltering(cam_first_satur, cam_second_satur, refmask_trial,
                           cam_first, cam_second);
        std::cout << "--> STEP: filtering done" << std::endl;
    } else {
        std::cout << "Skipping image filtering" << std::endl;
        cam_first = cam_first_satur;
        cam_second = cam_second_satur;
    }
    cv::imwrite("cam_first_filtered.png", cam_first[0]);
    cv::imwrite("cam_second_filtered.png", cam_second[0]);
    
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
    Utils::getCamerasForPair(stereopair, base_params_.cam_1, base_params_.cam_2);
    // Set output path
    base_params_.outputPath = Utils::buildPath(base_params_, true, true, true, false);
    
    // Create output directory
    std::filesystem::create_directories(base_params_.outputPath);
    
    // Set file paths
    base_params_.roifile = Utils::buildRoiFilePath(base_params_, reftrial, stereopair);
    base_params_.matchingfile = Utils::buildMatchingFilePath(base_params_, reftrial, stereopair);
    base_params_.seedfile = Utils::buildSeedFilePath(base_params_, reftrial, stereopair);
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
    std::string protocol_dir = Utils::buildProtocolDir(config_, true, true, true, true);
    std::cout << "protocol_path: " << protocol_dir << std::endl;
    
    // Find MAT file in protocol directory
    auto mat_files = Utils::findFiles(protocol_dir, "*.mat");
    
    if (mat_files.empty()) {
        std::cerr << "No protocol file found in: " << protocol_dir << std::endl;
        return false;
    }
    
    // Load first MAT file found using MatReader
    std::string protocol_file = mat_files[0];
    std::cout << "Loading protocol: " << protocol_file << std::endl;
    
    cppxdic::ProtocolFileData protocol_data;
    if (!MatReader::loadProtocol(protocol_file, protocol_data)) {
        std::cerr << "Failed to parse protocol file: " << protocol_file << std::endl;
        return false;
    }
    
    // Convert ProtocolFileData -> ProtocolInfo
    // Index 0 is a dummy entry so that 1-based trial indexing works directly
    // (MATLAB trial "001" -> index 1, "002" -> index 2, etc.)
    protocol_info_.dircond.clear();
    protocol_info_.nfcond.clear();
    protocol_info_.spdcond.clear();
    protocol_info_.repcond.clear();
    protocol_info_.spddxlcond.clear();
    
    // Dummy index 0
    protocol_info_.dircond.push_back("");
    protocol_info_.nfcond.push_back(0);
    protocol_info_.spdcond.push_back(0);
    protocol_info_.repcond.push_back(0);
    protocol_info_.spddxlcond.push_back(0.0);
    
    for (const auto& entry : protocol_data.trials) {
        protocol_info_.dircond.push_back(entry.direction);
        protocol_info_.nfcond.push_back(static_cast<int>(entry.force));
        protocol_info_.spdcond.push_back(static_cast<int>(entry.speed));
        protocol_info_.repcond.push_back(entry.repetition);
        protocol_info_.spddxlcond.push_back(0.08);
    }
    
    std::cout << "Protocol loaded: " << protocol_data.trials.size() << " trials" << std::endl;
    if (config_.debug_mode) {
        for (size_t i = 1; i < protocol_info_.dircond.size(); ++i) {
            std::cout << "  Trial " << i << ": dir=" << protocol_info_.dircond[i]
                      << " nf=" << protocol_info_.nfcond[i]
                      << " spd=" << protocol_info_.spdcond[i]
                      << " rep=" << protocol_info_.repcond[i] << std::endl;
        }
    }
    return true;
}

std::string StepDWorkflow::determineReferenceTrial(const std::string& trial) {
    // Manual override from config
    if (config_.ref_trial_id > 0) {
        std::ostringstream oss;
        oss << std::setw(3) << std::setfill('0') << config_.ref_trial_id;
        std::cout << "Reference trial (manual override): " << oss.str() << std::endl;
        return oss.str();
    }
    
    // Protocol-based reference trial selection (matching MATLAB stepD_2DDIC.m)
    unsigned int trial_idx = std::stoi(trial);
    
    if (trial_idx > 0 && trial_idx < protocol_info_.nfcond.size()) {
        int trial_nf = protocol_info_.nfcond[trial_idx];
        
        // Phase "loading" OR trial nf==1 → ref = first trial with nf==1 AND dir=="Ubnf"
        if (config_.phase_id == "loading" || trial_nf == 1) {
            for (size_t i = 1; i < protocol_info_.nfcond.size(); ++i) {
                if (protocol_info_.nfcond[i] == 1 &&
                    protocol_info_.dircond[i] == "Ubnf") {
                    std::ostringstream oss;
                    oss << std::setw(3) << std::setfill('0') << i;
                    std::cout << "Reference trial (nf=1, Ubnf): " << oss.str() << std::endl;
                    return oss.str();
                }
            }
        }
        // Phase "slide1" AND trial nf==5 → ref = first trial with nf==5 AND dir=="Ubnf"
        else if (config_.phase_id == "slide1" && trial_nf == 5) {
            for (size_t i = 1; i < protocol_info_.nfcond.size(); ++i) {
                if (protocol_info_.nfcond[i] == 5 &&
                    protocol_info_.dircond[i] == "Ubnf") {
                    std::ostringstream oss;
                    oss << std::setw(3) << std::setfill('0') << i;
                    std::cout << "Reference trial (nf=5, Ubnf): " << oss.str() << std::endl;
                    return oss.str();
                }
            }
        }
        
        std::cerr << "Warning: No matching reference trial found for trial " << trial
                  << " (phase=" << config_.phase_id << ", nf=" << trial_nf << ")" << std::endl;
    } else {
        std::cerr << "Warning: Trial index " << trial_idx << " out of protocol range" << std::endl;
    }
    
    // Fallback to current trial
    std::cout << "Reference trial (fallback): " << trial << std::endl;
    return trial;
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
                                        SeedPoint& after_disp_seed_point) {
    // Load or create ROI
    refmask_REF = ROIManager::loadOrCreateROI(base_params_, cam_first_satur[0]);
    if (refmask_REF.empty()) {
        return false;
    }
    
    // Load or create seed (seed.pw = pixel world coordinates)
    ref_seed_point = ROIManager::loadOrCreateSeed(base_params_, refmask_REF);
    ref_seed_point.sw = ROIManager::mapPixel2Subset(ref_seed_point.pw,
                                                           step1_params_.spacing);
    
    // Check for MATCHING file (ncorr binary, saved directly in output dir)
    if (!std::filesystem::exists(base_params_.matchingfile)) {
        // No matching file - compute it
        std::cout << "\n--> STEP: MATCHING file computation)" << std::endl;
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
        matchingInitialFrame(reftrial_cam_first_raw, 
                            cam_first_satur, 
                            refmask_REF,
                            base_params_.matchingfile,
                            message_oss.str(), 
                            ref_seed_point, 
                            refmask_trial, 
                            after_disp_seed_point);
    } else {
        std::cout << "Checkpoint found: " << base_params_.matchingfile << std::endl;
        std::cout << "--> STEP: MATCHING file loaded from checkpoint (skipped computation)" << std::endl;
    }

    // Load matching displacement fields from ncorr binary to map seed point
    if (std::filesystem::exists(base_params_.matchingfile)) {
        auto dic_output = ncorr::DIC_analysis_output::load(base_params_.matchingfile);
        
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
            
            refmask_trial = refmask_REF.clone();
            
            after_disp_seed_point.sw = ROIManager::mapPointCoordinate(ref_seed_point.sw,
                                                                        U_mapped, V_mapped);
            after_disp_seed_point.pw = ROIManager::mapSubset2Pixel(after_disp_seed_point.sw,
                                                                     step1_params_.spacing);
            
            std::cout << "--> STEP: MATCHING transformation applied" << std::endl;
            return true;
        } else {
            std::cerr << "Warning: MATCHING file has no displacements" << std::endl;
        }
    }
    
    std::cerr << "Warning: Something went wrong for the MATCHING" << std::endl;
    return false;
}

bool StepDWorkflow::matchingInitialFrame(const std::vector<cv::Mat>& cam_ref,
                                   const std::vector<cv::Mat>& cam_cur,
                                   const cv::Mat& refmask_ref,
                                   const std::string ncorr_matching_path,
                                   const std::string message,
                                   const SeedPoint& ref_seed_point,
                                   cv::Mat& refmask_cur_matched,
                                   SeedPoint& after_disp_seed_point) {
    std::cout << message << std::endl;
    
    // Prepare images: ref = cam_ref[0], cur = [cam_cur[0]]
    std::vector<cv::Mat> cur_imgs = {cam_cur[0]};
    
    // Set seed for matching
    step1_2_params_.initial_seed = {
        static_cast<int>(ref_seed_point.pw[0]),
        static_cast<int>(ref_seed_point.pw[1])
    };
    
    // Run ncorr analysis and save to cache-compatible path (for formatOutput to find)
    auto dic_output = runNcorrAnalysis(cam_ref[0], cur_imgs, refmask_ref,
                                      ref_seed_point, step1_2_params_,
                                      ncorr_matching_path, false, true); // false for parallel processing, true for no update
    
    // Extract matched ROI and displacement fields
    /*if (!dic_output.disps.empty()) {
        std::cout << "DEBUG - It works fine finding the DIC result" << std::endl;
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
        after_disp_seed_point.sw = ROIManager::mapPointCoordinate(
            ref_seed_point.sw, U_mapped, V_mapped);
        after_disp_seed_point.pw = ROIManager::mapSubset2Pixel(
            after_disp_seed_point.sw, step1_2_params_.spacing);

        std::cout << "DEBUG - after_disp_seed_point = " << after_disp_seed_point.pw[0] << ", " << after_disp_seed_point.pw[1] << std::endl;
    } else {
        std::cerr << "No displacement output from matching" << std::endl;
        return false;
    }*/
    
    std::cout << "--> STEP: " << message << " done" << std::endl;
    return true;
}

bool StepDWorkflow::performMatching(const std::vector<cv::Mat>& cam_first_satur,
                                   const std::vector<cv::Mat>& cam_second_satur,
                                   cv::Mat& refmask,
                                   SeedPoint& ref_seed_point,
                                   cv::Mat& after_disp_mask,
                                   SeedPoint& after_disp_seed_point) {
    std::cout << "MATCHING STEP - RUN #1" << std::endl;
    
    // Get camera numbers
    int cam_1, cam_2;
    Utils::getCamerasForPair(base_params_.stereopair, cam_1, cam_2);
    
    // ncorr matching output path (always .bin — ncorr native format)
    std::string ncorr_matching_path = base_params_.outputPath + "/ncorr" + 
        std::to_string(cam_1) + std::to_string(cam_2) + ".bin";
    
    // Compute matching if output doesn't exist
    if (!std::filesystem::exists(ncorr_matching_path)) {
        std::cout << "\n--> STEP: MATCHING file computation)" << std::endl;

        std::ostringstream message_oss;
        message_oss << "Matching Inside Camera Pair (1-2) : Cam1's frame 1 VS cam2's frame 1";

        matchingInitialFrame(cam_first_satur, 
                            cam_second_satur, 
                            refmask, 
                            ncorr_matching_path, 
                            message_oss.str(), 
                            ref_seed_point, 
                            after_disp_mask, 
                            after_disp_seed_point);
    } else {
        std::cout << "Checkpoint found: " << ncorr_matching_path << std::endl;
        std::cout << "--> STEP: MATCHING loaded from checkpoint (skipped computation)" << std::endl;
    }

    // Load matching displacement fields from ncorr binary to map seed point
    if (std::filesystem::exists(ncorr_matching_path)) {
        auto dic_output = ncorr::DIC_analysis_output::load(ncorr_matching_path);
        
        if (!dic_output.disps.empty()) {
            after_disp_mask = refmask.clone();
            
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
            
            after_disp_seed_point.sw = ROIManager::mapPointCoordinate(
                ref_seed_point.sw, U_mapped, V_mapped);
            after_disp_seed_point.pw = ROIManager::mapSubset2Pixel(
                after_disp_seed_point.sw, step1_params_.spacing);
            
            std::cout << "--> STEP: MATCHING transformation applied" << std::endl;
            return true;
        } else {
            std::cerr << "Warning: Matching file has no displacements" << std::endl;
        }
    }
    
    std::cerr << "Warning: Something went wrong with the matching" << std::endl;
    return false;
}

bool StepDWorkflow::performTracking(const int tracking_number,
                         const std::vector<cv::Mat>& cam_frames,
                         const cv::Mat& refmask,
                         const SeedPoint& initial_seed_point) {
    // Get actual camera number for this pair
    int cam_1, cam_2;
    Utils::getCamerasForPair(base_params_.stereopair, cam_1, cam_2);

    auto cam_number = tracking_number == 1 ? cam_1 : cam_2;

    std::cout << "TRACKING STEP " << tracking_number << std::endl;
    
    std::string output_path = base_params_.outputPath + "/ncorr" + std::to_string(cam_number) + ".bin";
    
    // Checkpoint: Check if ncorr binary already exists
    if (std::filesystem::exists(output_path)) {
        std::cout << "Checkpoint found: " << output_path << std::endl;
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
    
    int cam_1, cam_2;
    Utils::getCamerasForPair(stereopair, cam_1, cam_2);
    
    // ncorr outputs live directly in the output directory as .bin (ncorr native format)
    std::string ncorr1_bin = base_params_.outputPath + "/ncorr" + std::to_string(cam_1) + ".bin";
    std::string ncorr2_bin = base_params_.outputPath + "/ncorr" + std::to_string(cam_2) + ".bin";
    std::string ncorr12_bin = base_params_.outputPath + "/ncorr" + std::to_string(cam_1) + std::to_string(cam_2) + ".bin";
    
    if (!std::filesystem::exists(ncorr1_bin) || !std::filesystem::exists(ncorr2_bin) || !std::filesystem::exists(ncorr12_bin)) {
        std::cerr << "Warning: cached ncorr result files not found" << std::endl;
        if (!std::filesystem::exists(ncorr1_bin)) std::cerr << "  Missing: " << ncorr1_bin << std::endl;
        if (!std::filesystem::exists(ncorr2_bin)) std::cerr << "  Missing: " << ncorr2_bin << std::endl;
        if (!std::filesystem::exists(ncorr12_bin)) std::cerr << "  Missing: " << ncorr12_bin << std::endl;
        return;
    }
    
    auto d_serializer = cppxdic::DataSerializer::create(config_.data_format);
    std::string output_file = base_params_.outputPath + "/myDIC2DpairResults_C_" + 
        std::to_string(cam_1) + "_C_" + std::to_string(cam_2) + d_serializer->extension();
    
    if (std::filesystem::exists(output_file)) {
        std::cout << "Checkpoint found: " << output_file << std::endl;
        return;
    }
    
    std::cout << "  Loading cached DIC outputs..." << std::endl;
    ncorr::DIC_analysis_output dic1 = ncorr::DIC_analysis_output::load(ncorr1_bin);
    ncorr::DIC_analysis_output dic2 = ncorr::DIC_analysis_output::load(ncorr2_bin);
    ncorr::DIC_analysis_output dic12 = ncorr::DIC_analysis_output::load(ncorr12_bin);
    
    if (dic1.disps.empty() || dic2.disps.empty() || dic12.disps.empty()) {
        std::cerr << "Error: DIC outputs are empty" << std::endl;
        return;
    }
    
    int n_frames = dic1.disps.size();
    // get_scalefactor() returns spacing+1 (same as MATLAB Factor = spacing+1)
    int Factor = dic1.disps[0].get_scalefactor();
    std::cout << "  Processing " << n_frames << " frames, Factor=" << Factor << std::endl;
    std::cout << "  Total frames: " << (n_frames * 2 + 1) << " (cam1: " << n_frames 
              << " + matching: 1 + cam2: " << n_frames << ")" << std::endl;
    
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
    
    // Resize for cam1 frames + matching frame + cam2 frames
    results.Points.resize(n_frames * 2 + 1);
    results.CorCoeffVec.resize(n_frames * 2 + 1);
    
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
        // Store correlation coefficients as vector per frame
        results.CorCoeffVec[ii] = corrcoef;
    }
    
    std::cout << "  Processing matching frame (ncorr12)..." << std::endl;
    // Matching frame goes at index n_frames (between cam1 and cam2)
    {
        const auto& disp12 = dic12.disps[0];  // Matching has only 1 frame
        const auto& u12_array = disp12.get_u().get_array();
        const auto& v12_array = disp12.get_v().get_array();
        
        std::vector<cv::Point2f> points;
        std::vector<double> corrcoef;
        points.reserve(Pref.size());
        corrcoef.reserve(Pref.size());
        
        int idx = 0;
        for (int y = 0; y < roi_mask.height(); ++y) {
            for (int x = 0; x < roi_mask.width(); ++x) {
                if (roi_mask(y, x)) {
                    double u = u12_array(y, x);
                    double v = v12_array(y, x);
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
        Points2D pts2d12;
        pts2d12.x.reserve(points.size());
        pts2d12.y.reserve(points.size());
        for (const auto& p : points) {
            pts2d12.x.push_back(static_cast<double>(p.x));
            pts2d12.y.push_back(static_cast<double>(p.y));
        }
        results.Points[n_frames] = std::move(pts2d12);
        results.CorCoeffVec[n_frames] = corrcoef;
    }
    
    // Pre-compute matching displacement mapping (MATLAB step2_dic_finish lines 166-200)
    // For each cam1 ROI point, compute the mapped position in cam2's reduced grid
    // and bilinear interpolation weights
    const auto& disp12_ref = dic12.disps[0];
    const auto& u12_full = disp12_ref.get_u().get_array();
    const auto& v12_full = disp12_ref.get_v().get_array();
    int H2 = dic2.disps[0].get_u().data_height();
    int W2 = dic2.disps[0].get_u().data_width();
    
    std::cout << "  Processing cam2 frames (with matching-based mapping)..." << std::endl;
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
                    // Matching displacement at cam1 grid position
                    double u12 = u12_full(y, x);
                    double v12 = v12_full(y, x);
                    
                    if (u12 == 0.0 && v12 == 0.0) {
                        // No matching data at this point
                        points.push_back(cv::Point2f(NAN, NAN));
                        corrcoef.push_back(NAN);
                        idx++;
                        continue;
                    }
                    
                    // Mapped position in cam2's reduced grid
                    double cam2_rx = static_cast<double>(x) + u12 / Factor;
                    double cam2_ry = static_cast<double>(y) + v12 / Factor;
                    
                    // Bilinear interpolation of cam2 displacement at mapped position
                    int x0 = static_cast<int>(std::floor(cam2_rx));
                    int y0 = static_cast<int>(std::floor(cam2_ry));
                    int x1 = x0 + 1;
                    int y1 = y0 + 1;
                    double fx = cam2_rx - x0;
                    double fy = cam2_ry - y0;
                    
                    double u2_mapped = 0.0, v2_mapped = 0.0;
                    if (x0 >= 0 && y0 >= 0 && x1 < W2 && y1 < H2) {
                        u2_mapped = (1-fx)*(1-fy)*u2_array(y0,x0) + fx*(1-fy)*u2_array(y0,x1)
                                  + (1-fx)*fy*u2_array(y1,x0) + fx*fy*u2_array(y1,x1);
                        v2_mapped = (1-fx)*(1-fy)*v2_array(y0,x0) + fx*(1-fy)*v2_array(y0,x1)
                                  + (1-fx)*fy*v2_array(y1,x0) + fx*fy*v2_array(y1,x1);
                    } else if (x0 >= 0 && y0 >= 0 && x0 < W2 && y0 < H2) {
                        int cx = std::min(std::max((int)std::round(cam2_rx), 0), W2-1);
                        int cy = std::min(std::max((int)std::round(cam2_ry), 0), H2-1);
                        u2_mapped = u2_array(cy, cx);
                        v2_mapped = v2_array(cy, cx);
                    }
                    
                    // Final position = Pref + matching_disp + mapped_cam2_disp
                    // (matching MATLAB: Uref = new_Uref + U1_2)
                    double total_u = u12 + u2_mapped;
                    double total_v = v12 + v2_mapped;
                    points.push_back(cv::Point2f(Pref[idx].x + total_u, Pref[idx].y + total_v));
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
        // Cam2 frames start at index n_frames + 1 (after matching frame)
        results.Points[n_frames + 1 + ii] = std::move(pts2d2);
        // Store correlation coefficients as vector per frame
        results.CorCoeffVec[n_frames + 1 + ii] = corrcoef;
    }
    
    std::cout << "  Creating Delaunay triangulation..." << std::endl;
    results.Faces = DelaunayTriangulation::compute(Pref);
    double max_edge = 1.1 * std::sqrt(2.0) * Factor;
    results.Faces = DelaunayTriangulation::filterByEdgeLength(results.Faces, Pref, max_edge);
    results.Faces = DelaunayTriangulation::flipOrientation(results.Faces);
    std::cout << "  Triangles: " << (results.Faces.size() / 3) << std::endl;
    
    results.FaceColors.resize(results.Faces.size() / 3, 128.0);
    
    std::cout << "  Writing results..." << std::endl;
    if (d_serializer->saveDIC2DPairResults(output_file, results)) {
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
    const bool go_parallel,
    const bool use_no_update) {
    
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

    cv::imwrite(temp_dir + "/roi_mask.png", get_cv_img(roi.get_mask(), 0, 255));
    
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
        use_no_update ? ncorr::DIC_analysis_config::NO_UPDATE : ncorr::DIC_analysis_config::KEEP_MOST_POINTS,
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
        std::cout << "Debug:: " << seed_point.pw[0] << " " << seed_point.pw[1] << std::endl;
        std::cout << "Debug:: " << seed_point.sw[0] << " " << seed_point.sw[1] << std::endl;
        dic_output_raw = ncorr::DIC_analysis_sequential(dic_input, {ncorr::SeedParams(seed_point.pw[0], seed_point.pw[1])}, false);
    }
    
    // Post-process with both perspectives
    std::cout << "Post-processing displacements..." << std::endl;
    
    // Step 1: Convert to Eulerian perspective with sign inversion (still in pixels)
    ncorr::DIC_analysis_output dic_eulerian_pixels = ncorr::change_perspective_with_inversion(
        dic_output_raw, 
        ncorr::INTERP::CUBIC_KEYS  // Use cubic interpolation for perspective change
    );
    
    // Step 3: Apply units to BOTH perspectives
    ncorr::DIC_analysis_output dic_lagrangian = ncorr::set_units(dic_output_raw, "mm", config_.units_per_pixel);
    ncorr::DIC_analysis_output dic_eulerian = ncorr::set_units(dic_eulerian_pixels, "mm", config_.units_per_pixel);
    
    std::cout << "  Created both Lagrangian and Eulerian perspectives" << std::endl;
    
    // Save debug videos if debug mode is enabled
    if (config_.debug_mode) {
        std::string video_dir = std::filesystem::path(output_path).parent_path().string() + "/debug_video/";
        std::filesystem::create_directories(video_dir);
        std::string base_name = std::filesystem::path(output_path).stem().string();
        double alpha = config_.video_alpha;
        double fps = static_cast<double>(config_.video_fps);
        
        std::cout << "  Saving debug DIC videos to " << video_dir << std::endl;
        try {
            ncorr::save_DIC_video(video_dir + base_name + "_v_eulerian.avi",
                           dic_input, dic_eulerian, ncorr::DISP::V, alpha, fps);
            ncorr::save_DIC_video(video_dir + base_name + "_u_eulerian.avi",
                           dic_input, dic_eulerian, ncorr::DISP::U, alpha, fps);
            std::cout << "  ✓ Debug DIC videos saved" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "  Warning: Failed to save debug videos: " << e.what() << std::endl;
        }
    }
    
    // Save ncorr output directly as binary (ncorr's native format)
    // output_path already has .bin extension
    save(dic_lagrangian, output_path);
    std::cout << "DIC analysis saved: " << output_path << std::endl;
    
    return dic_lagrangian;
}

} // namespace cppxdic
