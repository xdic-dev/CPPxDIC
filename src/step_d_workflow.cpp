/**
 * DIC2D workflow for CPPXDIC
 * Implementation of the complete 2D DIC workflow
 * Based on MATLAB xDIC stepD_2DDIC.m
 */

#include "step_d_workflow.h"
#include "Array2D.h"
#include "mat_writer.h"
#include "mat_reader.h"
#include "cppxdic/pipeline/dic2d_frame_preparer.h"
#include "cppxdic/pipeline/dic2d_matching_service.h"
#include "cppxdic/pipeline/dic2d_ncorr_runner.h"
#include "cppxdic/pipeline/dic2d_output_formatter.h"
#include "cppxdic/pipeline/dic2d_tracking_service.h"
#include "ncorr.h"
#include "utils.h"
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cmath>
#include <cctype>

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
    pipeline::Dic2DMatchingService matching_service(
        config_, base_params_, step1_params_, step1_2_params_);
    pipeline::Dic2DNcorrRunner ncorr_runner(config_, base_params_);
    pipeline::Dic2DTrackingService tracking_service(
        config_, base_params_, step1_params_, step2_params_, ncorr_runner);
    pipeline::Dic2DOutputFormatter output_formatter(config_, base_params_, step1_params_);

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
    if (!matching_service.initializeROIAndSeed(frames.cam_first_saturated,
                                               refmask_REF,
                                               refmask_trial,
                                               ref_seed_point,
                                               initial_seed_point_set1)) {
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
    if (!matching_service.performStereoPairMatching(frames.cam_first_saturated,
                                                    frames.cam_second_saturated,
                                                    refmask_trial,
                                                    initial_seed_point_set1,
                                                    refmask_trial_matched,
                                                    initial_seed_point_set2)) {
        std::cerr << "Failed matching step" << std::endl;
        return {"", {}, false};
    }
    
    // IV. Save trial information
    saveTrialInfo(trial, stereopair, static_cast<int>(frames.cam_first_filtered.size()));
    
    // V. Tracking camera 1
    std::cout << "\nPerforming tracking camera 1..." << std::endl;
    if (!tracking_service.trackCamera1(frames.cam_first_filtered,
                                       refmask_trial,
                                       initial_seed_point_set1)) {
        std::cerr << "Failed tracking1 step" << std::endl;
        return {"", {}, false};
    }
    
    // VI. Tracking camera 2
    std::cout << "\nPerforming tracking camera 2..." << std::endl;
    if (!tracking_service.trackCamera2(frames.cam_second_filtered,
                                       refmask_trial_matched,
                                       initial_seed_point_set2)) {
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
    output_formatter.format(stereopair, pairOrder, pairForced);
    
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

} // namespace cppxdic
