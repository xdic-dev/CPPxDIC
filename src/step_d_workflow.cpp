/**
 * DIC2D workflow for CPPXDIC
 * Implementation of the complete 2D DIC workflow
 * Based on MATLAB xDIC stepD_2DDIC.m
 */

#include "step_d_workflow.h"
#include "Array2D.h"
#include "mat_writer.h"
#include "mat_reader.h"
#include "ncorr.h"
#include "utils.h"
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cmath>
#include <map>
#include <cctype>
#include <array>
#include <limits>
#include <algorithm>

namespace {

cv::Mat makeDebugCanvas(const cv::Mat& gray) {
    cv::Mat normalized;
    if (gray.empty()) {
        return normalized;
    }
    if (gray.type() == CV_8UC1) {
        normalized = gray.clone();
    } else {
        cv::normalize(gray, normalized, 0, 255, cv::NORM_MINMAX, CV_8U);
    }
    cv::Mat color;
    cv::cvtColor(normalized, color, cv::COLOR_GRAY2BGR);
    return color;
}

void overlayMask(cv::Mat& canvas, const cv::Mat& mask, const cv::Scalar& color) {
    if (canvas.empty() || mask.empty()) {
        return;
    }
    cv::Mat mask_binary;
    if (mask.type() == CV_8UC1) {
        cv::threshold(mask, mask_binary, 0, 255, cv::THRESH_BINARY);
    } else {
        mask.convertTo(mask_binary, CV_8U);
        cv::threshold(mask_binary, mask_binary, 0, 255, cv::THRESH_BINARY);
    }
    cv::Mat overlay(canvas.size(), canvas.type(), color);
    cv::Mat blended;
    cv::addWeighted(canvas, 0.75, overlay, 0.25, 0.0, blended);
    blended.copyTo(canvas, mask_binary);
}

void drawSeed(cv::Mat& canvas, const cppxdic::SeedPoint& seed, const cv::Scalar& color) {
    if (canvas.empty() || seed.pw.size() < 2) {
        return;
    }
    const cv::Point center(seed.pw[0] - 1, seed.pw[1] - 1);
    cv::drawMarker(canvas, center, color, cv::MARKER_CROSS, 18, 2);
    cv::circle(canvas, center, 5, color, 2);
}

void drawSeedDisplacement(cv::Mat& canvas,
                          const cppxdic::SeedPoint& before,
                          const cppxdic::SeedPoint& after,
                          const cv::Scalar& color) {
    if (canvas.empty() || before.pw.size() < 2 || after.pw.size() < 2) {
        return;
    }
    const cv::Point p0(before.pw[0] - 1, before.pw[1] - 1);
    const cv::Point p1(after.pw[0] - 1, after.pw[1] - 1);
    cv::arrowedLine(canvas, p0, p1, color, 2, cv::LINE_AA, 0, 0.15);
}

std::string sanitizeStageName(const std::string& stage_name) {
    std::string out = stage_name;
    for (char& ch : out) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '_';
        }
    }
    return out;
}

} // namespace

namespace cppxdic {

Dic2DWorkflow::Dic2DWorkflow(const Config& config) : config_(config) {
    // Initialize with default DIC constants
    setupStepParameters();
}

std::tuple<std::string, std::vector<int>, bool> 
Dic2DWorkflow::execute(const std::string& trial, int stereopair) {
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
    pipeline::PreparedDic2DFrames frames;
    pipeline::Dic2DFramePreparer frame_preparer(config_);
    std::cout << "Reading video data... ";
    if (!frame_preparer.importVideoFrames(trial, stereopair,
                                          frames.cam_first_raw,
                                          frames.cam_second_raw)) {
        std::cerr << "Failed to import video frames" << std::endl;
        return {"", {}, false};
    }
    std::cout << "Reading done. Frames: " << frames.cam_first_raw.size() << std::endl;
    
    // 4. Phase-specific frame selection
    frame_preparer.trimForPhase(frames);
    
    // 5. Saturation
    std::cout << "Applying saturation..." << std::endl;
    frame_preparer.performSaturation(frames.cam_first_raw,
                                     frames.cam_second_raw,
                                     base_params_.limit_grayscale,
                                     frames.cam_first_saturated,
                                     frames.cam_second_saturated);
    
    // II. ROI, Seed, and Matching REF to Trial at frame 1
    cv::Mat refmask_REF, refmask_trial;
    SeedPoint ref_seed_point, initial_seed_point_set1;
    std::cout << "Initializing ROI, seed, and matching REF to Trial..." << std::endl;
    if (!initializeROIAndSeed(frames.cam_first_saturated, refmask_REF, refmask_trial,
                             ref_seed_point, initial_seed_point_set1)) {
        std::cerr << "Failed to initialize ROI and seed" << std::endl;
        return {"", {}, false};
    }

    std::cout << "--> STEP: ROI loaded and formatted" << std::endl;
    std::cout << "--> STEP: SEED loaded and formatted" << std::endl;
    std::cout << "--> STEP: Matching REF to Trial loaded and formatted" << std::endl;
    
    // post-III. Image filtering
    if(config_.im_filter_mode) {
        std::cout << "Applying image filtering..." << std::endl;
        frame_preparer.applyImageFiltering(frames.cam_first_saturated,
                                           frames.cam_second_saturated,
                                           refmask_trial,
                                           frames.cam_first_filtered,
                                           frames.cam_second_filtered);
        std::cout << "--> STEP: filtering done" << std::endl;
    } else {
        std::cout << "Skipping image filtering" << std::endl;
        frames.cam_first_filtered = frames.cam_first_saturated;
        frames.cam_second_filtered = frames.cam_second_saturated;
    }

    // III. Matching inside a Trial between cameras (cam1 -> cam2 at frame 1)
    cv::Mat refmask_trial_matched;
    SeedPoint initial_seed_point_set2;
    std::cout << "\nPerforming camera matching..." << std::endl;
    if (!performMatching(frames.cam_first_saturated, frames.cam_second_saturated, refmask_trial,
                        initial_seed_point_set1, refmask_trial_matched,
                        initial_seed_point_set2)) {
        std::cerr << "Failed matching step" << std::endl;
        return {"", {}, false};
    }
    
    // IV. Save trial information
    saveTrialInfo(trial, stereopair, static_cast<int>(frames.cam_first_filtered.size()));
    
    // V. Tracking camera 1
    std::cout << "\nPerforming tracking camera 1..." << std::endl;
    if (!performTracking1(frames.cam_first_filtered, refmask_trial, initial_seed_point_set1)) {
        std::cerr << "Failed tracking1 step" << std::endl;
        return {"", {}, false};
    }
    
    // VI. Tracking camera 2
    std::cout << "\nPerforming tracking camera 2..." << std::endl;
    if (!performTracking2(frames.cam_second_filtered, refmask_trial_matched, initial_seed_point_set2)) {
        std::cerr << "Failed tracking2 step" << std::endl;
        return {"", {}, false};
    }
    
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

    // Post-preps. Format output
    formatOutput(trial, stereopair, pairOrder, pairForced);
    
    std::cout << "--> STEP: Ncorr analysis completed" << std::endl;
    
    return {base_params_.outputPath, pairOrder, pairForced};
}

void Dic2DWorkflow::setupBaseParameters(const std::string& trial,
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

void Dic2DWorkflow::setupStepParameters() {
    // Setup tracking parameters (camera 1) — defaults from DICConstants,
    // then override with config values from dic_params.txt (step_d_* keys)
    step1_params_.type = config_.step_d.analysis_type;
    step1_params_.radius = config_.step_d.radius;
    step1_params_.spacing = config_.step_d.spacing;
    step1_params_.cutoff_diffnorm = config_.step_d.cutoff_diffnorm;
    step1_params_.cutoff_iteration = config_.step_d.cutoff_iteration;
    step1_params_.total_threads = config_.step_d.total_threads;
    step1_params_.stepanalysis_params.enabled = config_.step_d.high_strain_enabled;
    step1_params_.stepanalysis_params.type = config_.step_d.seed_type;
    step1_params_.stepanalysis_params.auto_update = config_.step_d.auto_update;
    step1_params_.stepanalysis_params.step = config_.step_d.step_ref_change;
    
    // Setup tracking parameters (camera 2) - same as camera 1
    step2_params_ = step1_params_;
    
    // Setup matching parameters (camera 1 -> camera 2)
    // Uses step_e config if available, with larger default radius
    step1_2_params_ = step1_params_;
    step1_2_params_.radius = config_.step_e.radius;
    step1_2_params_.cutoff_diffnorm = config_.step_e.cutoff_diffnorm;
}

bool Dic2DWorkflow::loadProtocol() {
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

std::string Dic2DWorkflow::determineReferenceTrial(const std::string& trial) {
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

bool Dic2DWorkflow::importVideoFrames(const std::string& trial,
                                      int stereopair,
                                      std::vector<cv::Mat>& cam_first_raw,
                                      std::vector<cv::Mat>& cam_second_raw) {
    return pipeline::Dic2DFramePreparer(config_).importVideoFrames(
        trial, stereopair, cam_first_raw, cam_second_raw);
}

void Dic2DWorkflow::performSaturation(const std::vector<cv::Mat>& cam_first_raw,
                                      const std::vector<cv::Mat>& cam_second_raw,
                                      std::vector<cv::Mat>& cam_first_satur,
                                      std::vector<cv::Mat>& cam_second_satur) {
    pipeline::Dic2DFramePreparer(config_).performSaturation(
        cam_first_raw,
        cam_second_raw,
        base_params_.limit_grayscale,
        cam_first_satur,
        cam_second_satur);
}

bool Dic2DWorkflow::initializeROIAndSeed(const std::vector<cv::Mat>& cam_first_satur,
                                         cv::Mat& refmask_REF,
                                         cv::Mat& refmask_trial,
                                         SeedPoint& ref_seed_point,
                                         SeedPoint& after_disp_seed_point) {
    std::vector<cv::Mat> reftrial_cam_first_raw;

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
        std::vector<cv::Mat> reftrial_cam_second_raw;
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
        if (!matchingInitialFrame(reftrial_cam_first_raw,
                                  cam_first_satur,
                                  refmask_REF,
                                  base_params_.matchingfile,
                                  message_oss.str(),
                                  ref_seed_point,
                                  refmask_trial,
                                  after_disp_seed_point)) {
            return false;
        }
    } else {
        std::cout << "Checkpoint found: " << base_params_.matchingfile << std::endl;
        std::cout << "--> STEP: MATCHING file loaded from checkpoint (skipped computation)" << std::endl;
    }

    // Load matching displacement fields from ncorr binary to map seed point
    if (std::filesystem::exists(base_params_.matchingfile)) {
        auto dic_output = ncorr::DIC_analysis_output::load(base_params_.matchingfile);

        if (updateMaskAndSeedFromOutput(refmask_REF, ref_seed_point, dic_output,
                                        refmask_trial, after_disp_seed_point)) {
            std::cout << "--> STEP: MATCHING transformation applied" << std::endl;
            if (config_.debug_mode) {
                if (reftrial_cam_first_raw.empty()) {
                    std::vector<cv::Mat> reftrial_cam_second_raw;
                    importVideoFrames(base_params_.reftrial,
                                      base_params_.stereopair,
                                      reftrial_cam_first_raw,
                                      reftrial_cam_second_raw);
                }
                writeMatchingDebugPanel("ipm",
                                        reftrial_cam_first_raw.empty() ? cam_first_satur[0] : reftrial_cam_first_raw[0],
                                        cam_first_satur[0],
                                        refmask_REF,
                                        refmask_trial,
                                        ref_seed_point,
                                        after_disp_seed_point,
                                        dic_output);
            }
            return true;
        } else {
            std::cerr << "Warning: MATCHING file has no displacements" << std::endl;
        }
    }
    
    std::cerr << "Warning: Something went wrong for the MATCHING" << std::endl;
    return false;
}

bool Dic2DWorkflow::matchingInitialFrame(const std::vector<cv::Mat>& cam_ref,
                                         const std::vector<cv::Mat>& cam_cur,
                                         const cv::Mat& refmask_ref,
                                         const std::string ncorr_matching_path,
                                         const std::string message,
                                         const SeedPoint& ref_seed_point,
                                         cv::Mat& refmask_cur_matched,
                                         SeedPoint& after_disp_seed_point) {
    std::cout << message << std::endl;
    
    // MATLAB uses a two-frame current stack during matching to avoid an ncorr edge case.
    std::vector<cv::Mat> cur_imgs;
    cur_imgs.reserve(2);
    cur_imgs.push_back(cam_cur[0]);
    cur_imgs.push_back(cam_ref[0]);
    
    // Set seed for matching
    step1_2_params_.initial_seed = {
        static_cast<int>(ref_seed_point.pw[0]),
        static_cast<int>(ref_seed_point.pw[1])
    };
    
    // Run ncorr analysis and save to cache-compatible path (for formatOutput to find)
    auto dic_output = runNcorrAnalysis(cam_ref[0], cur_imgs, refmask_ref,
                                      ref_seed_point, step1_2_params_,
                                      ncorr_matching_path, false, true); // false for parallel processing, true for no update

    if (!updateMaskAndSeedFromOutput(refmask_ref, ref_seed_point, dic_output,
                                     refmask_cur_matched, after_disp_seed_point)) {
        std::cerr << "No displacement output from matching" << std::endl;
        return false;
    }

    std::cout << "--> STEP: " << message << " done" << std::endl;
    return true;
}

bool Dic2DWorkflow::performMatching(const std::vector<cv::Mat>& cam_first_satur,
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

        if (updateMaskAndSeedFromOutput(refmask, ref_seed_point, dic_output,
                                        after_disp_mask, after_disp_seed_point)) {
            std::cout << "--> STEP: MATCHING transformation applied" << std::endl;
            if (config_.debug_mode) {
                writeMatchingDebugPanel("icm",
                                        cam_first_satur[0],
                                        cam_second_satur[0],
                                        refmask,
                                        after_disp_mask,
                                        ref_seed_point,
                                        after_disp_seed_point,
                                        dic_output);
            }
            return true;
        } else {
            std::cerr << "Warning: Matching file has no displacements" << std::endl;
        }
    }
    
    std::cerr << "Warning: Something went wrong with the matching" << std::endl;
    return false;
}

bool Dic2DWorkflow::performTracking(const int tracking_number,
                                    const std::vector<cv::Mat>& cam_frames,
                                    const cv::Mat& refmask,
                                    const SeedPoint& initial_seed_point) {
    // Get actual camera number for this pair
    int cam_1, cam_2;
    Utils::getCamerasForPair(base_params_.stereopair, cam_1, cam_2);

    auto cam_number = tracking_number == 1 ? cam_1 : cam_2;

    std::cout << "\nTRACKING STEP " << tracking_number << std::endl;
    
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
    } else {
        step_params_ = step2_params_;
    }
    step_params_.initial_seed = {
        static_cast<int>(initial_seed_point.pw[0]),
        static_cast<int>(initial_seed_point.pw[1])
    };

    std::vector<cv::Mat> cur_frames = cam_frames;
    if (tracking_number == 2 && !cur_frames.empty()) {
        cur_frames.erase(cur_frames.begin());
    }
    if (cur_frames.empty()) {
        std::cerr << "No current frames available for tracking " << tracking_number << std::endl;
        return false;
    }
    
    runNcorrAnalysis(cam_frames[0], cur_frames, refmask,
                     initial_seed_point, step_params_,
                     output_path, config_.parallel_processing, true);
    
    std::cout << "--> STEP: Ncorr " << cam_number << " done and saved to " << output_path << std::endl;
    return true;
}

bool Dic2DWorkflow::performTracking1(const std::vector<cv::Mat>& cam_first,
                                     const cv::Mat& refmask_trial,
                                     const SeedPoint& initial_seed_point_set1) {
    std::cout << "Performing tracking camera 1..." << initial_seed_point_set1.pw[0] << "," << initial_seed_point_set1.pw[1] << std::endl;
    return performTracking(1, cam_first, refmask_trial, initial_seed_point_set1);
}

bool Dic2DWorkflow::performTracking2(const std::vector<cv::Mat>& cam_second,
                                     const cv::Mat& refmask_trial_matched,
                                     const SeedPoint& initial_seed_point_set2) {
    std::cout << "Performing tracking camera 2..." << initial_seed_point_set2.pw[0] << "," << initial_seed_point_set2.pw[1] << std::endl;
    return performTracking(2, cam_second, refmask_trial_matched, initial_seed_point_set2);
}

void Dic2DWorkflow::applyImageFiltering(const std::vector<cv::Mat>& cam_first_satur,
                                        const std::vector<cv::Mat>& cam_second_satur,
                                        const cv::Mat& refmask_trial,
                                        std::vector<cv::Mat>& cam_first,
                                        std::vector<cv::Mat>& cam_second) {
    pipeline::Dic2DFramePreparer(config_).applyImageFiltering(
        cam_first_satur,
        cam_second_satur,
        refmask_trial,
        cam_first,
        cam_second);
}

void Dic2DWorkflow::saveTrialInfo(const std::string& trial, int stereopair, int num_frames) {
    (void)trial;
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

void Dic2DWorkflow::formatOutput(const std::string& trial,
                                 int stereopair,
                                 const std::vector<int>& pairOrder,
                                 bool pairForced) {
    (void)trial;
    pipeline::Dic2DOutputFormatter(config_, base_params_, step1_params_)
        .format(stereopair, pairOrder, pairForced);
}

bool Dic2DWorkflow::updateMaskAndSeedFromOutput(const cv::Mat& input_mask,
                                                const SeedPoint& input_seed,
                                                const ncorr::DIC_analysis_output& dic_output,
                                                cv::Mat& output_mask,
                                                SeedPoint& output_seed) const {
    if (dic_output.disps.empty()) {
        return false;
    }

    const auto& disp = dic_output.disps.front();
    const auto& u_data = disp.get_u();
    const auto& v_data = disp.get_v();
    const auto& u_array = u_data.get_array();
    const auto& v_array = v_data.get_array();
    const double scale = static_cast<double>(disp.get_scalefactor());

    cv::Mat U_mapped(u_data.data_height(), u_data.data_width(), CV_64F);
    cv::Mat V_mapped(v_data.data_height(), v_data.data_width(), CV_64F);
    for (size_t y = 0; y < static_cast<size_t>(u_data.data_height()); ++y) {
        for (size_t x = 0; x < static_cast<size_t>(u_data.data_width()); ++x) {
            U_mapped.at<double>(static_cast<int>(y), static_cast<int>(x)) = u_array(y, x) / scale;
            V_mapped.at<double>(static_cast<int>(y), static_cast<int>(x)) = v_array(y, x) / scale;
        }
    }

    output_seed.sw = ROIManager::mapPointCoordinate(input_seed.sw, U_mapped, V_mapped);
    output_seed.pw = ROIManager::mapSubset2Pixel(output_seed.sw, static_cast<int>(scale - 1.0));

    output_mask = input_mask.clone();
    try {
        ncorr::ROI2D roi_current = ROIManager::matToNcorrROI(input_mask);
        ncorr::ROI2D roi_updated = ncorr::update(roi_current, disp, ncorr::INTERP::CUBIC_KEYS, ncorr::ROI_UPDATE_MODE::SKIP_INVALID);
        output_mask = ROIManager::ncorrROIToMat(roi_updated);
    } catch (const std::exception& e) {
        std::cerr << "  Warning: failed to update ROI through displacement field: "
                  << e.what() << std::endl;
    }

    return true;
}

void Dic2DWorkflow::writeNcorrMatSidecar(const std::string& output_path,
                                         const cv::Mat& ref_img,
                                         const std::vector<cv::Mat>& cur_imgs,
                                         const cv::Mat& roi_mask,
                                         const StepParameters& step_params,
                                         const ncorr::DIC_analysis_output& dic_output) const {
    if (cur_imgs.empty()) {
        return;
    }

    const std::filesystem::path mat_path = std::filesystem::path(output_path).replace_extension(".mat");
    std::vector<cv::Mat> cur_rois(cur_imgs.size(), roi_mask.clone());
    std::vector<ncorr::DIC_analysis_output> dic_outputs = {dic_output};

    std::map<std::string, double> dispinfo = {
        {"cutoff_corrcoef", config_.ncorr_cutoff_corrcoef},
        {"cutoff_diffnorm", step_params.cutoff_diffnorm},
        {"cutoff_iteration", static_cast<double>(step_params.cutoff_iteration)},
        {"lenscoef", 0.0},
        {"pixtounits", config_.units_per_pixel},
        {"radius", static_cast<double>(step_params.radius)},
        {"spacing", static_cast<double>(step_params.spacing)},
        {"subsettrunc", 0.0},
        {"total_threads", static_cast<double>(step_params.total_threads)}
    };

    if (!MatWriter::writeMultiFrameNcorrFile(mat_path.string(),
                                             ref_img,
                                             cur_imgs,
                                             roi_mask,
                                             cur_rois,
                                             dic_outputs,
                                             dispinfo)) {
        std::cerr << "  Warning: failed to write ncorr MAT sidecar: "
                  << mat_path << std::endl;
    }
}

void Dic2DWorkflow::writeMatchingDebugPanel(const std::string& stage_name,
                                            const cv::Mat& ref_img,
                                            const cv::Mat& cur_img,
                                            const cv::Mat& mask_before,
                                            const cv::Mat& mask_after,
                                            const SeedPoint& seed_before,
                                            const SeedPoint& seed_after,
                                            const ncorr::DIC_analysis_output& dic_output) const {
    if (!config_.debug_mode || ref_img.empty() || cur_img.empty()) {
        return;
    }

    cv::Mat ref_overlay = makeDebugCanvas(ref_img);
    cv::Mat cur_overlay = makeDebugCanvas(cur_img);
    cv::Mat before_mask_view = makeDebugCanvas(mask_before);
    cv::Mat after_mask_view = makeDebugCanvas(mask_after);

    overlayMask(ref_overlay, mask_before, cv::Scalar(60, 200, 255));
    overlayMask(cur_overlay, mask_after, cv::Scalar(60, 255, 120));
    overlayMask(before_mask_view, mask_before, cv::Scalar(0, 0, 255));
    overlayMask(after_mask_view, mask_after, cv::Scalar(0, 255, 0));

    drawSeed(ref_overlay, seed_before, cv::Scalar(0, 0, 255));
    drawSeed(cur_overlay, seed_after, cv::Scalar(0, 255, 255));
    drawSeedDisplacement(ref_overlay, seed_before, seed_after, cv::Scalar(255, 255, 0));

    const cv::Size panel_size(std::max(ref_overlay.cols, cur_overlay.cols),
                              std::max(ref_overlay.rows, cur_overlay.rows));
    cv::resize(ref_overlay, ref_overlay, panel_size, 0, 0, cv::INTER_NEAREST);
    cv::resize(cur_overlay, cur_overlay, panel_size, 0, 0, cv::INTER_NEAREST);
    cv::resize(before_mask_view, before_mask_view, panel_size, 0, 0, cv::INTER_NEAREST);
    cv::resize(after_mask_view, after_mask_view, panel_size, 0, 0, cv::INTER_NEAREST);

    cv::Mat top_row;
    cv::Mat bottom_row;
    cv::hconcat(ref_overlay, cur_overlay, top_row);
    cv::hconcat(before_mask_view, after_mask_view, bottom_row);

    cv::Mat panel;
    cv::vconcat(top_row, bottom_row, panel);

    const cv::Point text_origin(18, 30);
    const cv::Scalar text_color(255, 255, 255);
    cv::putText(panel, "Stage: " + stage_name, text_origin,
                cv::FONT_HERSHEY_SIMPLEX, 0.8, text_color, 2, cv::LINE_AA);

    const double dx = static_cast<double>(seed_after.pw[0] - seed_before.pw[0]);
    const double dy = static_cast<double>(seed_after.pw[1] - seed_before.pw[1]);
    std::ostringstream disp_text;
    disp_text << "Seed disp: (" << dx << ", " << dy << ")";
    cv::putText(panel, disp_text.str(), cv::Point(18, 62),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, text_color, 2, cv::LINE_AA);

    if (!dic_output.disps.empty()) {
        const auto& cc_array = dic_output.disps.front().get_cc().get_array();
        const int seed_x = std::clamp(seed_before.sw[0], 0, static_cast<int>(cc_array.width()) - 1);
        const int seed_y = std::clamp(seed_before.sw[1], 0, static_cast<int>(cc_array.height()) - 1);
        std::ostringstream cc_text;
        cc_text << "Seed corrcoef: " << cc_array(seed_y, seed_x);
        cv::putText(panel, cc_text.str(), cv::Point(18, 94),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, text_color, 2, cv::LINE_AA);
    }

    const std::filesystem::path debug_dir = std::filesystem::path(base_params_.outputPath) / "debug_stepd";
    std::filesystem::create_directories(debug_dir);
    const std::filesystem::path debug_file = debug_dir / (sanitizeStageName(stage_name) + ".png");
    cv::imwrite(debug_file.string(), panel);
}

ncorr::DIC_analysis_output Dic2DWorkflow::runNcorrAnalysis(
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
        dic_output_raw = ncorr::matlab_DIC_analysis_parallel(dic_parallel_input);
    } else {
        std::cout << "  Using Matlab-style sequential DIC processing..." << std::endl;
        dic_output_raw = ncorr::matlab_DIC_analysis_sequential(
            dic_input,
            {ncorr::SeedParams(seed_point.pw[0], seed_point.pw[1])},
            false
        );
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
    // IMPORTANT: Save raw pixel displacements, NOT mm-scaled.
    // dic3DReconstruction and formatOutput need pixel-coordinate displacements
    // because they combine (x*scalefactor + displacement) for DLT reconstruction.
    save(dic_output_raw, output_path);
    writeNcorrMatSidecar(output_path, ref_img, cur_imgs, roi_mask, step_params, dic_output_raw);
    std::cout << "DIC analysis saved: " << output_path << std::endl;
    
    return dic_output_raw;
}

} // namespace cppxdic
