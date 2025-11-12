/**
 * DIC Analysis class for CPPXDIC
 * Main analysis functionality equivalent to Matlab dic_analysis.m
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
    
    bool run();
    
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
