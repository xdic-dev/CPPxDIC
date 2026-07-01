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
#include "matlab_functions.h"
#include "logging.h"
#include "dic/cuncorr_dic.h"
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

StepDWorkflow::StepDWorkflow(const Config& config) : config_(config) {
    // Initialize with default DIC constants
    setupStepParameters();
}

std::tuple<std::string, std::vector<int>, bool> 
StepDWorkflow::execute(const std::string& trial, int stereopair, const StagePlan& plan) {
    LOG_INFO << "-------------------------------------------";
    LOG_INFO << "-------------------------------------------";
    LOG_INFO << "Digital Image Correlation analysis launch";

    // CHECKPOINT SYSTEM (matching MATLAB implementation):
    // The following checkpoint files are checked/created to avoid redundant computation:
    // 1. REF_MASK_{reftrial}_{phase}_pair{stereopair}.mat - ROI mask (loaded from base path)
    // 2. REF_SEED_{reftrial}_{phase}_pair{stereopair}.mat - Seed point (loaded from base path)
    // 3. MATCHING2{reftrial}_pair{stereopair}.bin - REF-to-trial matching (in output path)
    // 4. ncorr{cam1}{cam2}.bin - Inter-camera matching (in output path)
    // 5. ncorr{cam1}.bin - Camera 1 tracking results (in output path)
    // 6. ncorr{cam2}.bin - Camera 2 tracking results (in output path)
    // 7. dic_info_data_target_pair{stereopair}.mat - Trial metadata (in output path)
    //
    // STAGE DECOMPOSITION (plan): every sub-step below checkpoints to / loads from
    // disk, so a process can run only some sub-steps and rely on a prior process's
    // outputs for the rest:
    //   plan.match  -> ROI/seed + REF->trial matching (3) + camera matching (4) + dic_info (7)
    //   plan.track  -> per-camera tracking (5,6); needs (3,4) on disk (loaded here)
    //   plan.format -> formatOutput (myDIC2DpairResults); needs (4,5,6) on disk

    // I. Preps analysis
    // 1. Load protocol and determine reference trial
    if (!loadProtocol()) {
        LOG_ERROR << "Failed to load protocol";
        return {"", {}, false};
    }

    std::string reftrial = determineReferenceTrial(trial);
    LOG_INFO << "Reference trial: " << reftrial;

    // 2. Setup parameters
    setupBaseParameters(trial, stereopair, reftrial);

    // Resolve this pair's camera ids for per-camera tracking selection.
    int cam_first_num = 0, cam_second_num = 0;
    Utils::getCamerasForPair(stereopair, cam_first_num, cam_second_num);
    const bool do_track1 = plan.track && (plan.only_cam == 0 || plan.only_cam == cam_first_num);
    const bool do_track2 = plan.track && (plan.only_cam == 0 || plan.only_cam == cam_second_num);
    // Camera matching is needed to (re)produce ncorr{cam1}{cam2} for the match stage,
    // and to recover refmask_trial_matched + seed for camera-2 tracking.
    const bool need_matching = plan.match || do_track2;
    // The frame stack (import + saturation + ROI/seed) is needed by matching/tracking
    // but NOT by a format-only stage (which just loads ncorr binaries from disk).
    const bool need_frames = plan.match || plan.track;

    if (need_frames) {
        // 3. Import video frames
        std::vector<cv::Mat> cam_first_raw, cam_second_raw;
        LOG_INFO << "Reading video data... ";
        if (!importVideoFrames(trial, stereopair, cam_first_raw, cam_second_raw)) {
            LOG_ERROR << "Failed to import video frames";
            return {"", {}, false};
        }
        LOG_INFO << "Reading done. Frames: " << cam_first_raw.size();

        // 4. Phase-specific frame selection
        if (config_.phase_id == "slide1") {
            size_t keep = cam_first_raw.size() / 2 + 5;
            cam_first_raw.resize(keep);
            cam_second_raw.resize(keep);
            LOG_INFO << "Phase 'slide1': keeping " << keep << " frames";
        }

        // 5. Saturation
        std::vector<cv::Mat> cam_first_satur, cam_second_satur;
        LOG_INFO << "Applying saturation...";
        performSaturation(cam_first_raw, cam_second_raw, cam_first_satur, cam_second_satur);

        // II. ROI, Seed, and Matching REF to Trial at frame 1
        //     (writes MATCHING2*.bin when absent; loads + applies it when present)
        cv::Mat refmask_REF, refmask_trial;
        SeedPoint ref_seed_point, initial_seed_point_set1;
        LOG_INFO << "Initializing ROI, seed, and matching REF to Trial...";
        if (!initializeROIAndSeed(cam_first_satur, refmask_REF, refmask_trial,
                                 ref_seed_point, initial_seed_point_set1)) {
            LOG_ERROR << "Failed to initialize ROI and seed";
            return {"", {}, false};
        }

        LOG_INFO << "--> STEP: ROI loaded and formatted";
        LOG_INFO << "--> STEP: SEED loaded and formatted";
        LOG_INFO << "--> STEP: Matching REF to Trial loaded and formatted";

        // post-III. Image filtering (only needed for tracking)
        std::vector<cv::Mat> cam_first, cam_second;
        if (plan.track) {
            if (config_.im_filter_mode) {
                LOG_INFO << "Applying image filtering...";
                applyImageFiltering(cam_first_satur, cam_second_satur, refmask_trial,
                                   cam_first, cam_second);
                LOG_INFO << "--> STEP: filtering done";
            } else {
                LOG_INFO << "Skipping image filtering";
                cam_first = cam_first_satur;
                cam_second = cam_second_satur;
            }
        }

        // III. Matching inside a Trial between cameras (cam1 -> cam2 at frame 1)
        //      (writes ncorr{cam1}{cam2}.bin when absent; loads it when present)
        cv::Mat refmask_trial_matched;
        SeedPoint initial_seed_point_set2;
        if (need_matching) {
            LOG_INFO << "\nPerforming camera matching...";
            if (!performMatching(cam_first_satur, cam_second_satur, refmask_trial,
                                initial_seed_point_set1, refmask_trial_matched,
                                initial_seed_point_set2)) {
                LOG_ERROR << "Failed matching step";
                return {"", {}, false};
            }
        }

        // IV. Save trial information (frame count metadata)
        saveTrialInfo(trial, stereopair, cam_first_satur.size());

        // V. Tracking camera 1
        if (do_track1) {
            LOG_INFO << "\nPerforming tracking camera 1...";
            if (!performTracking1(cam_first, refmask_trial, initial_seed_point_set1)) {
                LOG_ERROR << "Failed tracking1 step";
                return {"", {}, false};
            }
        }

        // VI. Tracking camera 2
        if (do_track2) {
            LOG_INFO << "\nPerforming tracking camera 2...";
            if (!performTracking2(cam_second, refmask_trial_matched, initial_seed_point_set2)) {
                LOG_ERROR << "Failed tracking2 step";
                return {"", {}, false};
            }
        }
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

    // Post-preps. Format output (loads ncorr{cam1},{cam2},{cam1cam2} -> myDIC2DpairResults)
    if (plan.format) {
        formatOutput(trial, stereopair, pairOrder, pairForced);
    }

    LOG_INFO << "--> STEP: Ncorr analysis completed";

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

bool StepDWorkflow::loadProtocol() {
    // Load protocol MAT file
    std::string protocol_dir = Utils::buildProtocolDir(config_, true, true, true, true);
    LOG_INFO << "protocol_path: " << protocol_dir;

    // Find MAT file in protocol directory
    auto mat_files = Utils::findFiles(protocol_dir, "*.mat");

    if (mat_files.empty()) {
        LOG_ERROR << "No protocol file found in: " << protocol_dir;
        return false;
    }

    // Load first MAT file found using MatReader
    std::string protocol_file = mat_files[0];
    LOG_INFO << "Loading protocol: " << protocol_file;

    cppxdic::ProtocolFileData protocol_data;
    if (!MatReader::loadProtocol(protocol_file, protocol_data)) {
        LOG_ERROR << "Failed to parse protocol file: " << protocol_file;
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
    
    LOG_INFO << "Protocol loaded: " << protocol_data.trials.size() << " trials";
    if (config_.debug_mode) {
        for (size_t i = 1; i < protocol_info_.dircond.size(); ++i) {
            LOG_DEBUG << "  Trial " << i << ": dir=" << protocol_info_.dircond[i]
                      << " nf=" << protocol_info_.nfcond[i]
                      << " spd=" << protocol_info_.spdcond[i]
                      << " rep=" << protocol_info_.repcond[i];
        }
    }
    return true;
}

std::string StepDWorkflow::determineReferenceTrial(const std::string& trial) {
    // Manual override from config
    if (config_.ref_trial_id > 0) {
        std::ostringstream oss;
        oss << std::setw(3) << std::setfill('0') << config_.ref_trial_id;
        LOG_INFO << "Reference trial (manual override): " << oss.str();
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
                    LOG_INFO << "Reference trial (nf=1, Ubnf): " << oss.str();
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
                    LOG_INFO << "Reference trial (nf=5, Ubnf): " << oss.str();
                    return oss.str();
                }
            }
        }
        
        LOG_WARN << "No matching reference trial found for trial " << trial
                  << " (phase=" << config_.phase_id << ", nf=" << trial_nf << ")";
    } else {
        LOG_WARN << "Trial index " << trial_idx << " out of protocol range";
    }

    // Fallback to current trial
    LOG_INFO << "Reference trial (fallback): " << trial;
    return trial;
}

bool StepDWorkflow::importVideoFrames(const std::string& trial,
                                     int stereopair,
                                     std::vector<cv::Mat>& cam_first_raw,
                                     std::vector<cv::Mat>& cam_second_raw,
                                     int maxFrames) {
    // Use Utils::importVid to get frame paths
    std::vector<std::string> cam1_frames, cam2_frames;

    // Convert trial string to integer for Utils::importVid
    int trial_num = std::stoi(trial);

    // Import video frames using Utils
    if (!Utils::importVid(config_, trial_num, stereopair, cam1_frames, cam2_frames, maxFrames)) {
        LOG_ERROR << "Failed to import video frames for trial " << trial
                  << " stereopair " << stereopair;
        return false;
    }

    // Check that we got frames
    if (cam1_frames.empty() || cam2_frames.empty()) {
        LOG_ERROR << "No frames imported for trial " << trial
                  << " stereopair " << stereopair;
        return false;
    }

    // Check frame count consistency
    if (cam1_frames.size() != cam2_frames.size()) {
        LOG_ERROR << "Frame count mismatch: cam1=" << cam1_frames.size()
                  << " cam2=" << cam2_frames.size();
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
            LOG_ERROR << "Failed to load frame: " << cam1_frames[i];
            return false;
        }
        
        // Load second camera frame
        cv::Mat img2 = cv::imread(cam2_frames[i], cv::IMREAD_GRAYSCALE);
        if (img2.empty()) {
            LOG_ERROR << "Failed to load frame: " << cam2_frames[i];
            return false;
        }
        
        cam_first_raw.push_back(img1);
        cam_second_raw.push_back(img2);
    }
    
    LOG_INFO << "Loaded " << cam_first_raw.size() << " frames for each camera";
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
        LOG_INFO << "\n--> STEP: MATCHING file computation)";
        std::vector<cv::Mat> reftrial_cam_second_raw;
        LOG_INFO << "Reading REF Trial video data...";
        // Only the REF trial's first frame is used for REF->trial matching, so
        // import a single frame instead of decoding the entire reference video.
        if (!importVideoFrames(base_params_.reftrial,
                            base_params_.stereopair,
                            reftrial_cam_first_raw,
                            reftrial_cam_second_raw,
                            /*maxFrames=*/1)) {
            LOG_ERROR << "Failed to import REF Trial video frames";
            return false;
        }
        LOG_INFO << "Reading REF Trial video data done. Frames: " << reftrial_cam_first_raw.size();
        
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
        LOG_INFO << "Checkpoint found: " << base_params_.matchingfile;
        LOG_INFO << "--> STEP: MATCHING file loaded from checkpoint (skipped computation)";
    }

    // Load matching displacement fields from ncorr binary to map seed point
    if (std::filesystem::exists(base_params_.matchingfile)) {
        auto dic_output = ncorr::DIC_analysis_output::load(base_params_.matchingfile);

        if (updateMaskAndSeedFromOutput(refmask_REF, ref_seed_point, dic_output,
                                        refmask_trial, after_disp_seed_point)) {
            LOG_INFO << "--> STEP: MATCHING transformation applied";
            if (config_.debug_mode) {
                if (reftrial_cam_first_raw.empty()) {
                    std::vector<cv::Mat> reftrial_cam_second_raw;
                    // Debug panel only needs the REF trial's first frame.
                    importVideoFrames(base_params_.reftrial,
                                      base_params_.stereopair,
                                      reftrial_cam_first_raw,
                                      reftrial_cam_second_raw,
                                      /*maxFrames=*/1);
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
            LOG_WARN << "MATCHING file has no displacements";
        }
    }

    LOG_WARN << "Something went wrong for the MATCHING";
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
    LOG_INFO << message;
    
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
        LOG_ERROR << "No displacement output from matching";
        return false;
    }

    LOG_INFO << "--> STEP: " << message << " done";
    return true;
}

bool StepDWorkflow::performMatching(const std::vector<cv::Mat>& cam_first_satur,
                                   const std::vector<cv::Mat>& cam_second_satur,
                                   cv::Mat& refmask,
                                   SeedPoint& ref_seed_point,
                                   cv::Mat& after_disp_mask,
                                   SeedPoint& after_disp_seed_point) {
    LOG_INFO << "MATCHING STEP - RUN #1";
    
    // Get camera numbers
    int cam_1, cam_2;
    Utils::getCamerasForPair(base_params_.stereopair, cam_1, cam_2);
    
    // ncorr matching output path (always .bin — ncorr native format)
    std::string ncorr_matching_path = base_params_.outputPath + "/ncorr" + 
        std::to_string(cam_1) + std::to_string(cam_2) + ".bin";
    
    // Compute matching if output doesn't exist
    if (!std::filesystem::exists(ncorr_matching_path)) {
        LOG_INFO << "\n--> STEP: MATCHING file computation)";

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
        LOG_INFO << "Checkpoint found: " << ncorr_matching_path;
        LOG_INFO << "--> STEP: MATCHING loaded from checkpoint (skipped computation)";
    }

    // Load matching displacement fields from ncorr binary to map seed point
    if (std::filesystem::exists(ncorr_matching_path)) {
        auto dic_output = ncorr::DIC_analysis_output::load(ncorr_matching_path);

        if (updateMaskAndSeedFromOutput(refmask, ref_seed_point, dic_output,
                                        after_disp_mask, after_disp_seed_point)) {
            LOG_INFO << "--> STEP: MATCHING transformation applied";
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
            LOG_WARN << "Matching file has no displacements";
        }
    }

    LOG_WARN << "Something went wrong with the matching";
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

    LOG_INFO << "\nTRACKING STEP " << tracking_number;
    
    std::string output_path = base_params_.outputPath + "/ncorr" + std::to_string(cam_number) + ".bin";
    
    // Checkpoint: Check if ncorr binary already exists
    if (std::filesystem::exists(output_path)) {
        LOG_INFO << "Checkpoint found: " << output_path;
        LOG_INFO << "--> STEP: Ncorr " << cam_number << " loaded from checkpoint (skipped computation)";
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
        LOG_ERROR << "No current frames available for tracking " << tracking_number;
        return false;
    }

    runNcorrAnalysis(cam_frames[0], cur_frames, refmask,
                     initial_seed_point, step_params_,
                     output_path, config_.parallel_processing, true);

    LOG_INFO << "--> STEP: Ncorr " << cam_number << " done and saved to " << output_path;
    return true;
}

bool StepDWorkflow::performTracking1(const std::vector<cv::Mat>& cam_first,
                                    const cv::Mat& refmask_trial,
                                    const SeedPoint& initial_seed_point_set1) {
    LOG_DEBUG << "Performing tracking camera 1..." << initial_seed_point_set1.pw[0] << "," << initial_seed_point_set1.pw[1];
    return performTracking(1, cam_first, refmask_trial, initial_seed_point_set1);
}

bool StepDWorkflow::performTracking2(const std::vector<cv::Mat>& cam_second,
                                    const cv::Mat& refmask_trial_matched,
                                    const SeedPoint& initial_seed_point_set2) {
    LOG_DEBUG << "Performing tracking camera 2..." << initial_seed_point_set2.pw[0] << "," << initial_seed_point_set2.pw[1];
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

void StepDWorkflow::formatOutput(const std::string& trial,
                                int stereopair,
                                const std::vector<int>& pairOrder,
                                bool pairForced) {
    (void)trial;
    LOG_INFO << "Formatting output files (step2_dic_finish equivalent)...";
    
    int cam_1, cam_2;
    Utils::getCamerasForPair(stereopair, cam_1, cam_2);
    
    // ncorr outputs live directly in the output directory as .bin (ncorr native format)
    std::string ncorr1_bin = base_params_.outputPath + "/ncorr" + std::to_string(cam_1) + ".bin";
    std::string ncorr2_bin = base_params_.outputPath + "/ncorr" + std::to_string(cam_2) + ".bin";
    std::string ncorr12_bin = base_params_.outputPath + "/ncorr" + std::to_string(cam_1) + std::to_string(cam_2) + ".bin";
    
    if (!std::filesystem::exists(ncorr1_bin) || !std::filesystem::exists(ncorr2_bin) || !std::filesystem::exists(ncorr12_bin)) {
        LOG_WARN << "cached ncorr result files not found";
        if (!std::filesystem::exists(ncorr1_bin)) LOG_WARN << "  Missing: " << ncorr1_bin;
        if (!std::filesystem::exists(ncorr2_bin)) LOG_WARN << "  Missing: " << ncorr2_bin;
        if (!std::filesystem::exists(ncorr12_bin)) LOG_WARN << "  Missing: " << ncorr12_bin;
        return;
    }
    
    auto d_serializer = cppxdic::DataSerializer::create(config_.data_format);
    std::string output_file = base_params_.outputPath + "/myDIC2DpairResults_C_" + 
        std::to_string(cam_1) + "_C_" + std::to_string(cam_2) + d_serializer->extension();
    
    if (std::filesystem::exists(output_file)) {
        LOG_INFO << "Checkpoint found: " << output_file;
        return;
    }

    LOG_INFO << "  Loading cached DIC outputs...";
    ncorr::DIC_analysis_output dic1 = ncorr::DIC_analysis_output::load(ncorr1_bin);
    ncorr::DIC_analysis_output dic2 = ncorr::DIC_analysis_output::load(ncorr2_bin);
    ncorr::DIC_analysis_output dic12 = ncorr::DIC_analysis_output::load(ncorr12_bin);
    
    if (dic1.disps.empty() || dic2.disps.empty() || dic12.disps.empty()) {
        LOG_ERROR << "DIC outputs are empty";
        return;
    }

    // Apply replacebadcorr if enabled (MATLAB step2_dic_finish equivalent)
    // This replaces badly correlated subsets with spatiotemporally filtered values
    if (config_.step_d_replacebadcorr) {
        LOG_INFO << "  Applying replacebadcorr (MATLAB step2_dic_finish)...";
        cppxdic::matlab_replacebadcorr(dic1);
        cppxdic::matlab_replacebadcorr(dic2);
        cppxdic::matlab_replacebadcorr(dic12);
    }

    const size_t n_frames_cam1 = dic1.disps.size();
    const size_t n_frames_cam2 = dic2.disps.size();
    const int Factor = dic1.disps[0].get_scalefactor(); // MATLAB: spacing + 1
    const double nan = std::numeric_limits<double>::quiet_NaN();

    LOG_INFO << "  Processing " << n_frames_cam1 << " cam1 frames, "
              << n_frames_cam2 << " cam2 frames, Factor=" << Factor;
    if (n_frames_cam2 + 1 != n_frames_cam1) {
        LOG_WARN << "cam2 tracking count differs from MATLAB expectation "
                  << "(expected " << (n_frames_cam1 - 1) << ", got " << n_frames_cam2 << ")";
    }

    DIC2DPairResults results;
    results.nCamRef = cam_1;
    results.nCamDef = cam_2;
    results.nImages = static_cast<int>(n_frames_cam1);
    results.pairOrder = pairOrder;
    results.pairForced = pairForced;
    results.ncorrInfo.cutoff_corrcoef = {
        config_.ncorr_cutoff_corrcoef,
        config_.ncorr_cutoff_corrcoef,
        config_.ncorr_cutoff_corrcoef
    };
    results.ncorrInfo.cutoff_diffnorm = step1_params_.cutoff_diffnorm;
    results.ncorrInfo.cutoff_iteration = step1_params_.cutoff_iteration;
    results.ncorrInfo.imgcorr = {"reference", "current"};
    results.ncorrInfo.lenscoef = 0;
    results.ncorrInfo.pixtounits = config_.units_per_pixel;
    results.ncorrInfo.radius = step1_params_.radius;
    results.ncorrInfo.spacing = step1_params_.spacing;
    results.ncorrInfo.stepanalysis.enabled = step1_params_.stepanalysis_params.enabled;
    results.ncorrInfo.stepanalysis.type = step1_params_.stepanalysis_params.type;
    results.ncorrInfo.stepanalysis.auto_update = step1_params_.stepanalysis_params.auto_update;
    results.ncorrInfo.stepanalysis.step = step1_params_.stepanalysis_params.step;
    results.ncorrInfo.subsettrunc = false;
    results.ncorrInfo.total_threads = step1_params_.total_threads;
    results.ncorrInfo.type = step1_params_.type;
    results.ncorrInfo.units = "pixels";
    
    const auto& roi1 = dic1.disps[0].get_roi();
    const auto& roi_mask = roi1.get_mask();
    results.ROImask = cv::Mat(roi_mask.height(), roi_mask.width(), CV_8U);
    std::vector<std::pair<int, int>> roi_coords;
    std::vector<cv::Point2d> Pref;

    for (int y = 0; y < roi_mask.height(); ++y) {
        for (int x = 0; x < roi_mask.width(); ++x) {
            results.ROImask.at<uint8_t>(y, x) = roi_mask(y, x) ? 255 : 0;
            if (roi_mask(y, x)) {
                roi_coords.emplace_back(y, x);
                Pref.emplace_back(static_cast<double>(x * Factor + 1),
                                  static_cast<double>(y * Factor + 1));
            }
        }
    }
    LOG_INFO << "  Reference points: " << Pref.size();

    const size_t n_total_frames = n_frames_cam1 + 1 + n_frames_cam2;
    results.Points.resize(n_total_frames);
    results.CorCoeffVec.resize(n_total_frames);

    auto fillDirectFrame = [&](const ncorr::Disp2D& disp, size_t out_idx) {
        const auto& u_array = disp.get_u().get_array();
        const auto& v_array = disp.get_v().get_array();
        const auto& cc_array = disp.get_cc().get_array();

        Points2D pts2d;
        pts2d.x.reserve(Pref.size());
        pts2d.y.reserve(Pref.size());
        std::vector<double> corrcoef;
        corrcoef.reserve(Pref.size());

        for (size_t idx = 0; idx < roi_coords.size(); ++idx) {
            const auto [y, x] = roi_coords[idx];
            double u = u_array(y, x);
            double v = v_array(y, x);
            double cc = cc_array(y, x);

            if (u == 0.0) u = nan;
            if (v == 0.0) v = nan;
            if (cc == 0.0) cc = nan;

            pts2d.x.push_back(std::isnan(u) ? nan : (Pref[idx].x + u));
            pts2d.y.push_back(std::isnan(v) ? nan : (Pref[idx].y + v));
            corrcoef.push_back(cc);
        }

        results.Points[out_idx] = std::move(pts2d);
        results.CorCoeffVec[out_idx] = std::move(corrcoef);
    };

    LOG_INFO << "  Processing cam1 frames...";
    for (size_t ii = 0; ii < n_frames_cam1; ++ii) {
        fillDirectFrame(dic1.disps[ii], ii);
    }

    LOG_INFO << "  Processing inter-camera matching frame...";
    fillDirectFrame(dic12.disps.front(), n_frames_cam1);

    const auto& disp12_ref = dic12.disps.front();
    const auto& u12_full = disp12_ref.get_u().get_array();
    const auto& v12_full = disp12_ref.get_v().get_array();

    LOG_INFO << "  Processing cam2 frames (mapped through matching)...";
    for (size_t ii = 0; ii < n_frames_cam2; ++ii) {
        const auto& disp2 = dic2.disps[ii];
        const auto& u2_array = disp2.get_u().get_array();
        const auto& v2_array = disp2.get_v().get_array();
        const auto& cc2_array = disp2.get_cc().get_array();
        const int H2 = disp2.get_u().data_height();
        const int W2 = disp2.get_u().data_width();

        auto interpolateWeighted = [&](const auto& arr, double rx, double ry) -> double {
            const int x0 = static_cast<int>(std::floor(rx));
            const int y0 = static_cast<int>(std::floor(ry));
            const int x1 = x0 + 1;
            const int y1 = y0 + 1;

            double numerator = 0.0;
            double denominator = 0.0;
            const std::array<std::pair<int, int>, 4> samples = {{
                {x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}
            }};

            for (const auto& [sx, sy] : samples) {
                if (sx < 0 || sy < 0 || sx >= W2 || sy >= H2) {
                    continue;
                }
                const double value = arr(sy, sx);
                if (value == 0.0) {
                    continue;
                }
                const double dx = rx - static_cast<double>(sx);
                const double dy = ry - static_cast<double>(sy);
                const double dist = std::sqrt(dx * dx + dy * dy);
                const double weight = dist < 1e-12 ? 1e12 : 1.0 / dist;
                numerator += value * weight;
                denominator += weight;
            }

            if (denominator > 0.0) {
                return numerator / denominator;
            }

            const int cx = std::clamp(static_cast<int>(std::round(rx)), 0, W2 - 1);
            const int cy = std::clamp(static_cast<int>(std::round(ry)), 0, H2 - 1);
            const double fallback = arr(cy, cx);
            return fallback == 0.0 ? nan : fallback;
        };

        Points2D pts2d;
        pts2d.x.reserve(Pref.size());
        pts2d.y.reserve(Pref.size());
        std::vector<double> corrcoef;
        corrcoef.reserve(Pref.size());

        for (size_t idx = 0; idx < roi_coords.size(); ++idx) {
            const auto [y, x] = roi_coords[idx];
            const double u12 = u12_full(y, x);
            const double v12 = v12_full(y, x);
            double cc = cc2_array(y, x);
            if (cc == 0.0) {
                cc = nan;
            }
            corrcoef.push_back(cc);

            if (u12 == 0.0 && v12 == 0.0) {
                pts2d.x.push_back(nan);
                pts2d.y.push_back(nan);
                continue;
            }

            const double cam2_rx = static_cast<double>(x) + u12 / static_cast<double>(Factor);
            const double cam2_ry = static_cast<double>(y) + v12 / static_cast<double>(Factor);
            const double u2_mapped = interpolateWeighted(u2_array, cam2_rx, cam2_ry);
            const double v2_mapped = interpolateWeighted(v2_array, cam2_rx, cam2_ry);

            if (std::isnan(u2_mapped) || std::isnan(v2_mapped)) {
                pts2d.x.push_back(nan);
                pts2d.y.push_back(nan);
                continue;
            }

            pts2d.x.push_back(Pref[idx].x + u12 + u2_mapped);
            pts2d.y.push_back(Pref[idx].y + v12 + v2_mapped);
        }

        results.Points[n_frames_cam1 + 1 + ii] = std::move(pts2d);
        results.CorCoeffVec[n_frames_cam1 + 1 + ii] = std::move(corrcoef);
    }
    
    LOG_INFO << "  Creating Delaunay triangulation...";
    std::vector<cv::Point2f> pref_float;
    pref_float.reserve(Pref.size());
    for (const auto& p : Pref) {
        pref_float.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
    }
    results.Faces = DelaunayTriangulation::compute(pref_float);
    double max_edge = 1.1 * std::sqrt(2.0) * Factor;
    results.Faces = DelaunayTriangulation::filterByEdgeLength(results.Faces, pref_float, max_edge);
    results.Faces = DelaunayTriangulation::flipOrientation(results.Faces);
    LOG_INFO << "  Triangles: " << (results.Faces.size() / 3);
    
    results.FaceColors.resize(results.Faces.size() / 3, 128.0);
    
    LOG_INFO << "  Writing results...";
    if (d_serializer->saveDIC2DPairResults(output_file, results)) {
        LOG_INFO << "Output formatting complete: " << output_file;
    } else {
        LOG_ERROR << "Failed to write DIC2DPairResults";
    }
}

bool StepDWorkflow::updateMaskAndSeedFromOutput(const cv::Mat& input_mask,
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
        LOG_WARN << "failed to update ROI through displacement field: "
                  << e.what();
    }

    return true;
}

void StepDWorkflow::writeNcorrMatSidecar(const std::string& output_path,
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
        LOG_WARN << "failed to write ncorr MAT sidecar: "
                  << mat_path;
    }
}

void StepDWorkflow::writeMatchingDebugPanel(const std::string& stage_name,
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

ncorr::DIC_analysis_output StepDWorkflow::runNcorrAnalysis(
    const cv::Mat& ref_img,
    const std::vector<cv::Mat>& cur_imgs,
    const cv::Mat& roi_mask,
    const SeedPoint& seed_point,
    const StepParameters& step_params,
    const std::string& output_path,
    const bool go_parallel,
    const bool use_no_update) {
    
    LOG_INFO << "Running ncorr DIC analysis...";
    LOG_DEBUG << "  Radius: " << step_params.radius << ", Spacing: " << step_params.spacing;
    LOG_DEBUG << "  Scalefactor: " << (step_params.spacing + 1) << " (spacing + 1)";
    LOG_DEBUG << "  Seed: (" << seed_point.pw[0] << ", " << seed_point.pw[1] << ")";
    
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
    
    // CRITICAL FIX: Override update_corrcoef to disable correlation-based reference updates.
    // MATLAB ncorr uses fixed-step reference changes (step_ref_change=10), not correlation-based.
    // The KEEP_MOST_POINTS preset sets update_corrcoef=0.5, which causes frequent ref updates
    // when correlation drops, leading to ROI fragmentation and divergence around frame 60-70.
    // Setting this to ncorr_cutoff_corrcoef (default 10.0) effectively disables correlation-based updates.
    dic_input.update_corrcoef = config_.ncorr_cutoff_corrcoef;
    
    // Run DIC analysis (returns Lagrangian perspective in pixels)
    ncorr::DIC_analysis_output dic_output_raw;

    const bool use_cuncorr = (config_.dic_engine != "ncorr");
    if (use_cuncorr) {
        // cuNCorr engine: CUDA if a device is present, else its CPU backend (identical
        // results). Produces the same raw Lagrangian/pixel DIC_analysis_output on the
        // reduced grid, so the downstream .bin + StepE reconstruction is unchanged.
        cppxdic::CuncorrDicConfig ccfg;
        ccfg.scalefactor = scalefactor;
        ccfg.subregion_radius = step_params.radius;
        ccfg.num_threads = step_params.total_threads;
        ccfg.seed_x = seed_point.pw[0];
        ccfg.seed_y = seed_point.pw[1];
        ccfg.seed_search = config_.cuncorr_seed_search;
        ccfg.debug = config_.debug_mode;
        cppxdic::CuncorrDicInfo cinfo;
        LOG_INFO << "  Using cuNCorr DIC engine...";
        dic_output_raw = cppxdic::run_cuncorr_dic(ncorr_images, roi, ccfg, &cinfo);
        LOG_INFO << "  cuNCorr backend: " << cinfo.backend << (cinfo.cuda ? " (CUDA)" : " (CPU)");
    } else if (go_parallel) {
        LOG_INFO << "  Using parallel DIC processing"
                  << (config_.ncorr_use_exact_matlab ? " (exact_matlab_*)" : " (matlab_*)")
                  << "...";

        // Create seed parameters from the seed point
        std::vector<ncorr::SeedParams> seeds;
        seeds.push_back(ncorr::SeedParams(seed_point.pw[0], seed_point.pw[1]));

        // Create parallel input structure
        ncorr::DIC_analysis_parallel_input dic_parallel_input(dic_input, seeds);

        // Run parallel DIC analysis
        dic_output_raw = config_.ncorr_use_exact_matlab
            ? ncorr::exact_matlab_DIC_analysis_parallel(dic_parallel_input)
            : ncorr::matlab_DIC_analysis_parallel(dic_parallel_input);
    } else {
        LOG_INFO << "  Using Matlab-style sequential DIC processing"
                  << (config_.ncorr_use_exact_matlab ? " (exact_matlab_*)" : " (matlab_*)")
                  << "...";
        dic_output_raw = config_.ncorr_use_exact_matlab
            ? ncorr::exact_matlab_DIC_analysis_sequential(
                dic_input,
                {ncorr::SeedParams(seed_point.pw[0], seed_point.pw[1])},
                false)
            : ncorr::matlab_DIC_analysis_sequential(
                dic_input,
                {ncorr::SeedParams(seed_point.pw[0], seed_point.pw[1])},
                false);
    }
    
    // Post-process with both perspectives
    LOG_INFO << "Post-processing displacements...";
    
    // Step 1: Convert to Eulerian perspective with sign inversion (still in pixels)
    ncorr::DIC_analysis_output dic_eulerian_pixels = ncorr::change_perspective_with_inversion(
        dic_output_raw, 
        ncorr::INTERP::CUBIC_KEYS  // Use cubic interpolation for perspective change
    );
    
    // Step 3: Apply units to BOTH perspectives
    ncorr::DIC_analysis_output dic_lagrangian = ncorr::set_units(dic_output_raw, "mm", config_.units_per_pixel);
    ncorr::DIC_analysis_output dic_eulerian = ncorr::set_units(dic_eulerian_pixels, "mm", config_.units_per_pixel);
    
    LOG_INFO << "  Created both Lagrangian and Eulerian perspectives";
    
    // Save debug videos if debug mode is enabled
    if (config_.debug_mode) {
        std::string video_dir = std::filesystem::path(output_path).parent_path().string() + "/debug_video/";
        std::filesystem::create_directories(video_dir);
        std::string base_name = std::filesystem::path(output_path).stem().string();
        double alpha = config_.video_alpha;
        double fps = static_cast<double>(config_.video_fps);
        
        LOG_INFO << "  Saving debug DIC videos to " << video_dir;
        try {
            ncorr::save_DIC_video(video_dir + base_name + "_v_eulerian.avi",
                           dic_input, dic_eulerian, ncorr::DISP::V, alpha, fps);
            ncorr::save_DIC_video(video_dir + base_name + "_u_eulerian.avi",
                           dic_input, dic_eulerian, ncorr::DISP::U, alpha, fps);
            LOG_INFO << "  ✓ Debug DIC videos saved";
        } catch (const std::exception& e) {
            LOG_WARN << "Failed to save debug videos: " << e.what();
        }
    }
    
    // Save ncorr output directly as binary (ncorr's native format)
    // output_path already has .bin extension
    // IMPORTANT: Save raw pixel displacements, NOT mm-scaled.
    // dic3DReconstruction and formatOutput need pixel-coordinate displacements
    // because they combine (x*scalefactor + displacement) for DLT reconstruction.
    save(dic_output_raw, output_path);
    writeNcorrMatSidecar(output_path, ref_img, cur_imgs, roi_mask, step_params, dic_output_raw);
    LOG_INFO << "DIC analysis saved: " << output_path;

    return dic_output_raw;
}

} // namespace cppxdic
