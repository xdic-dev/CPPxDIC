/**
 * DIC Analysis implementation for CPPXDIC
 */

#include "dic_analysis.h"
#include "mat_reader.h"
#include "utils.h"
#include "step_d_workflow.h"
#include "data_serializer.h"
#include "cppxdic/pipeline/step_e_runner.h"
#include "cppxdic/pipeline/step_f_runner.h"
#include "cppxdic/pipeline/trial_selector.h"
#include "strain_computation.h"
#include "surface_stitching.h"
#include "temporal_filter.h"
#include "face_isotropy.h"
#include "visualization.h"
#include <iostream>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <matio.h>
#include <limits>
#include <Eigen/Dense>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <array>
#include <numeric>
#include <set>

using namespace ncorr;
using namespace cppxdic;

DicAnalysis::DicAnalysis(const Config& config) : config_(config) {
}

bool DicAnalysis::dicDeformationAnalysis(const std::vector<int>& trial_target) {
    return cppxdic::pipeline::StepFRunner(config_).run(trial_target);
}
bool DicAnalysis::dic3DReconstruction(const std::vector<int>& trial_target) {
    return cppxdic::pipeline::StepERunner(config_).run(trial_target);
}

bool DicAnalysis::setupNcorrAnalysis(const std::vector<std::string>& images,
                                    const std::string& roi_mask_image_path,
                                    DIC_analysis_input& dic_input) {
    try {
        if (images.size() < 2) {
            std::cerr << "Need at least 2 images for DIC analysis" << std::endl;
            return false;
        }

        std::vector<Image2D> ncorr_images;
        for (const auto& img_path : images) {
            ncorr_images.emplace_back(img_path);
        }

        // Build ROI from provided mask image (non-zero pixels inside ROI)
        Image2D roi_mask(roi_mask_image_path);
        ROI2D roi(roi_mask.get_gs() > 0.5);

        dic_input = DIC_analysis_input(
            ncorr_images,
            roi,
            3,
            INTERP::QUINTIC_BSPLINE_PRECOMPUTE,
            SUBREGION::CIRCLE,
            config_.subregion_radius,
            4,
            DIC_analysis_config::NO_UPDATE,
            config_.debug_mode
        );

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error setting up ncorr analysis with ROI mask: " << e.what() << std::endl;
        return false;
    }
}

bool DicAnalysis::run() {
    // Search for trial targets (equivalent to search_trial2target)
    std::vector<int> trial_target = searchTrialTarget();
    
    std::cout << "Trial target set: [";
    for (size_t i = 0; i < trial_target.size(); ++i) {
        std::cout << trial_target[i];
        if (i < trial_target.size() - 1) std::cout << ", ";
    }
    std::cout << "]" << std::endl;
    
    // Create serializer for format-aware checkpoint checks
    auto serializer = cppxdic::DataSerializer::create(config_.data_format);
    std::string ext = serializer->extension();
    
    // Helper lambda: Check if 2D DIC outputs exist for all trials/pairs
    auto check_2d_outputs_exist = [&]() -> bool {
        for (int trial : trial_target) {
            auto output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);
            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                int cam1, cam2;
                Utils::getCamerasForPair(pair, cam1, cam2);

                std::string path = Utils::buildDic2DPairResultsFilePath(output_dir, cam1, cam2, ext);
                if (!std::filesystem::exists(path)) {
                    return false;
                }
            }
        }
        return true;
    };
    
    // Helper lambda: Check if 3D reconstruction outputs exist
    auto check_3d_outputs_exist = [&]() -> bool {
        for (int trial : trial_target) {
            auto output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);
            std::string dic3d_file = Utils::buildDic3DCombinedFilePath(output_dir, config_.num_pair, ext);
            if (!std::filesystem::exists(dic3d_file)) {
                return false;
            }
        }
        return true;
    };
    
    // Helper lambda: Check if deformation analysis outputs exist
    auto check_deformation_outputs_exist = [&]() -> bool {
        for (int trial : trial_target) {
            auto output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);
            std::string pp_file = Utils::buildDic3DPPresultsFilePath(output_dir, config_.num_pair, config_.fileversion, ext);
            if (!std::filesystem::exists(pp_file)) {
                return false;
            }
        }
        return true;
    };
    
    // STEP D: 2D-DIC
    bool step_d_complete = check_2d_outputs_exist();
    if (step_d_complete) {
        std::cout << "\n=== STEP D: 2D-DIC ===" << std::endl;
        std::cout << "✓ Checkpoint detected: All 2D DIC output files exist" << std::endl;
        std::cout << "  Skipping 2D analysis (use existing results)" << std::endl;
    } else {
        std::cout << "\n=== STEP D: 2D-DIC ===" << std::endl;
        std::cout << "Running 2D DIC analysis..." << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        bool success = dic2DAnalysis(trial_target);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        if (!success) {
            std::cerr << "2D DIC Analysis failed!" << std::endl;
            return false;
        }
        
        std::cout << "✓ DIC 2D Analysis done in " << duration.count() / 1000.0 << " s" << std::endl;
    }
    
    // STEP E: 3D Reconstruction
    bool step_e_complete = check_3d_outputs_exist();
    if (step_e_complete) {
        std::cout << "\n=== STEP E: 3D Reconstruction ===" << std::endl;
        std::cout << "✓ Checkpoint detected: All 3D reconstruction output files exist" << std::endl;
        std::cout << "  Skipping 3D reconstruction (use existing results)" << std::endl;
    } else {
        if (!step_d_complete) {
            std::cout << "\n=== STEP E: 3D Reconstruction ===" << std::endl;
        }
        std::cout << "Running 3D reconstruction..." << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        bool success = dic3DReconstruction(trial_target);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        if (!success) {
            std::cerr << "3D Reconstruction failed!" << std::endl;
            return false;
        }
        
        std::cout << "✓ DIC 3D Reconstruction done in " << duration.count() / 1000.0 << " s" << std::endl;
    }
    
    // STEP F: Deformation analysis
    bool step_f_complete = check_deformation_outputs_exist();
    if (step_f_complete) {
        std::cout << "\n=== STEP F: Deformation Analysis ===" << std::endl;
        std::cout << "✓ Checkpoint detected: Deformation analysis output exists" << std::endl;
        std::cout << "  Skipping deformation analysis (use existing results)" << std::endl;
    } else {
        if (!step_e_complete) {
            std::cout << "\n=== STEP F: Deformation Analysis ===" << std::endl;
        }
        std::cout << "Running deformation analysis..." << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        bool success = dicDeformationAnalysis(trial_target);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        if (!success) {
            std::cerr << "Deformation Analysis failed!" << std::endl;
            return false;
        }
        
        std::cout << "✓ DIC Deformation Analysis done in " << duration.count() / 1000.0 << " s" << std::endl;
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "✓ All DIC analysis steps complete!" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return true;
}

std::vector<int> DicAnalysis::searchTrialTarget() {
    return cppxdic::pipeline::TrialSelector(config_).selectTargets();
}

bool DicAnalysis::dic2DAnalysis(const std::vector<int>& trial_target) {
    std::cout << "Starting 2D DIC Analysis (using StepDWorkflow)..." << std::endl;
    
    try {
        // Create workflow instance
        StepDWorkflow workflow(config_);
        
        // Process each trial and stereo pair
        for (int trial : trial_target) {
            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                // Format trial as 3-digit string (e.g., "005")
                std::ostringstream trial_str;
                trial_str << std::setw(3) << std::setfill('0') << trial;
                
                std::cout << "\n========================================" << std::endl;
                std::cout << "Processing Trial " << trial << ", Pair " << pair << std::endl;
                std::cout << "========================================" << std::endl;
                
                // Execute workflow
                auto [outputPath, pairOrder, pairForced] = workflow.execute(trial_str.str(), pair);
                
                if (outputPath.empty()) {
                    std::cerr << "Workflow failed for trial " << trial << ", pair " << pair << std::endl;
                    return false;
                }
                
                std::cout << "✓ Trial " << trial << ", pair " << pair << " completed successfully." << std::endl;
                std::cout << "  Output path: " << outputPath << std::endl;
                std::cout << "  Pair order: [" << pairOrder[0] << ", " << pairOrder[1] << "]" << std::endl;
                std::cout << "  Pair forced: " << (pairForced ? "true" : "false") << std::endl;
            }
        }
        
        std::cout << "\n✓ All 2D DIC Analysis completed successfully!" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in 2D DIC Analysis: " << e.what() << std::endl;
        return false;
    }
}


std::vector<std::string> DicAnalysis::loadImageSequence(const std::string& trial_path) {
    std::vector<std::string> images;
    
    // Look for common image formats
    std::vector<std::string> extensions = {".png", ".jpg", ".jpeg", ".tiff", ".bmp"};
    
    try {
        if (std::filesystem::exists(trial_path)) {
            for (const auto& entry : std::filesystem::directory_iterator(trial_path)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().string();
                    for (const auto& ext : extensions) {
                        if (filename.size() >= ext.size() && 
                            filename.compare(filename.size() - ext.size(), ext.size(), ext) == 0) {
                            images.push_back(filename);
                            break;
                        }
                    }
                }
            }
        }
        
        // Sort images to ensure proper sequence
        std::sort(images.begin(), images.end());
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading image sequence: " << e.what() << std::endl;
    }
    
    return images;
}

bool DicAnalysis::setupNcorrAnalysis(const std::vector<std::string>& images, 
                                   DIC_analysis_input& dic_input) {
    try {
        if (images.size() < 2) {
            std::cerr << "Need at least 2 images for DIC analysis" << std::endl;
            return false;
        }
        
        // Convert string paths to Image2D objects
        std::vector<Image2D> ncorr_images;
        for (const auto& img_path : images) {
            ncorr_images.emplace_back(img_path);
        }
        
        Image2D first_image(images[0]);
        ROI2D roi(first_image.get_gs() > 0.1);
        
        // Setup DIC input with parameters similar to the ncorr test
        dic_input = DIC_analysis_input(
            ncorr_images,                                    // Images
            roi,                                            // ROI
            3,                                              // scalefactor
            INTERP::QUINTIC_BSPLINE_PRECOMPUTE,            // Interpolation
            SUBREGION::CIRCLE,                             // Subregion shape
            config_.subregion_radius,                      // Subregion radius
            4,                                             // # of threads
            DIC_analysis_config::NO_UPDATE,                // DIC configuration
            config_.debug_mode                             // Debugging enabled/disabled
        );
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error setting up ncorr analysis: " << e.what() << std::endl;
        return false;
    }
}
