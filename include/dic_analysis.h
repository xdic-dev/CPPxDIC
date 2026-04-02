/**
 * DIC Analysis class for CPPXDIC
 * Main analysis functionality equivalent to Matlab dic_analysis.m
 * 
 * This class orchestrates the three main DIC analysis steps:
 * - DIC2D analysis (Dic2DWorkflow)
 * - 3D reconstruction (Reconstruction3DRunner)
 * - Deformation analysis (DeformationRunner)
 * 
 * Each step is implemented as a separate workflow class with clear
 * inputs, outputs, and testable milestone functions.
 */

#ifndef DIC_ANALYSIS_H
#define DIC_ANALYSIS_H

#include "config.h"
#include "ncorr.h"
#include <vector>
#include <string>

class DicAnalysis {
public:
    explicit DicAnalysis(const Config& config);
    
    /**
     * Run complete DIC analysis pipeline
     * Executes preprocessing, DIC2D, reconstruction, and deformation in sequence
     * 
     * @return true if all steps complete successfully
     */
    bool run();

    /**
     * Run DIC2D analysis only — for integration testing
     * Requires source videos / ROI / seed configuration to exist
     */
    bool runDic2D(const std::vector<int>& trial_target) { return dic2DAnalysis(trial_target); }
    bool runStepD(const std::vector<int>& trial_target) { return runDic2D(trial_target); }
    
    /**
     * Run 3D reconstruction only — for integration testing
     * Requires DIC2D outputs in binary cache form and calibration files to exist
     */
    bool runReconstruction3D(const std::vector<int>& trial_target) { return dic3DReconstruction(trial_target); }
    bool runStepE(const std::vector<int>& trial_target) { return runReconstruction3D(trial_target); }
    
    /**
     * Run deformation analysis only — for integration testing
     * Requires DIC3Dcombined_*.bin from reconstruction to exist
     */
    bool runDeformation(const std::vector<int>& trial_target) { return dicDeformationAnalysis(trial_target); }
    bool runStepF(const std::vector<int>& trial_target) { return runDeformation(trial_target); }
    
private:
    const Config& config_;
    
    // Analysis steps
    bool dic2DAnalysis(const std::vector<int>& trial_target);
    bool dic3DReconstruction(const std::vector<int>& trial_target);
    bool dicDeformationAnalysis(const std::vector<int>& trial_target);
    
    // Helper functions
    std::vector<int> searchTrialTarget();
    std::vector<std::string> loadImageSequence(const std::string& trial_path);
    bool setupNcorrAnalysis(const std::vector<std::string>& images, 
                           ncorr::DIC_analysis_input& dic_input);
    bool setupNcorrAnalysis(const std::vector<std::string>& images,
                            const std::string& roi_mask_image_path,
                            ncorr::DIC_analysis_input& dic_input);
};

#endif // DIC_ANALYSIS_H
