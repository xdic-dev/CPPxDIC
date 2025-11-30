/**
 * Preprocessing Workflow implementation for CPPXDIC
 */

#include "preprocessing_workflow.h"
#include "utils.h"
#include <matio.h>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cctype>

namespace cppxdic {

PreprocessingWorkflow::PreprocessingWorkflow(const Config& config) 
    : config_(config) {
    validatePaths();
}

void PreprocessingWorkflow::validatePaths() {
    // Validate data_path exists
    if (!std::filesystem::exists(config_.data_path)) {
        throw std::runtime_error("Data path does not exist: " + config_.data_path);
    }
    
    // Validate dic_path exists or can be created
    if (!std::filesystem::exists(config_.dic_path)) {
        std::filesystem::create_directories(config_.dic_path);
    }
}

bool PreprocessingWorkflow::execute() {
    std::cout << "=== Preprocessing Workflow ===" << std::endl;
    
    // Step 1: Load protocol
    auto protocol_opt = loadProtocol();
    if (!protocol_opt) {
        std::cerr << "Failed to load protocol" << std::endl;
        return false;
    }
    protocol_ = *protocol_opt;
    std::cout << "✓ Protocol loaded" << std::endl;
    
    // Step 2: Search for target trials
    target_trials_ =  {7, 12, 25};//searchTrialTargets(protocol_);
    if (target_trials_.empty()) {
        std::cerr << "No target trials found" << std::endl;
        return false;
    }
    std::cout << "✓ Found " << target_trials_.size() << " target trials" << std::endl;
    
    // Step 3: Validate inputs for each trial
    for (int trial : target_trials_) {
        for (int pair = 1; pair <= config_.num_pair; ++pair) {
            if (!validateTrialInputs(trial, pair)) {
                std::cerr << "Warning: Missing inputs for trial " << trial 
                          << ", pair " << pair << std::endl;
            }
        }
    }
    
    is_ready_ = true;
    std::cout << "✓ Preprocessing complete" << std::endl;
    return true;
}

std::optional<ProtocolData> PreprocessingWorkflow::loadProtocol() {
    std::string protocol_path = getProtocolPath();
    
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
        return std::nullopt;
    }
    
    // Load first MAT file found
    std::string protocol_file = mat_files[0];
    std::cout << "Loading protocol: " << protocol_file << std::endl;
    
    mat_t* matfp = Mat_Open(protocol_file.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open protocol file" << std::endl;
        return std::nullopt;
    }
    
    ProtocolData protocol;
    
    // Read 'cond' struct
    matvar_t* cond = Mat_VarRead(matfp, "cond");
    if (!cond || cond->class_type != MAT_C_STRUCT) {
        if (cond) Mat_VarFree(cond);
        Mat_Close(matfp);
        std::cerr << "Variable 'cond' not found or not a struct" << std::endl;
        
        // Return default protocol data
        protocol.dircond = {"", "Ubnf", "Rbnf", "Ubnf", "Rbnf", "Ubnf"};
        protocol.nfcond = {0, 1, 1, 5, 5, 1};
        protocol.spdcond = {0, 1, 1, 1, 1, 1};
        protocol.repcond = {0, 1, 1, 1, 1, 1};
        return protocol;
    }
    
    // TODO: Complete implementation of protocol parsing
    // For now, use default data
    protocol.dircond = {"", "Ubnf", "Rbnf", "Ubnf", "Rbnf", "Ubnf"};
    protocol.nfcond = {0, 1, 1, 5, 5, 1};
    protocol.spdcond = {0, 1, 1, 1, 1, 1};
    protocol.repcond = {0, 1, 1, 1, 1, 1};
    
    if (cond) Mat_VarFree(cond);
    Mat_Close(matfp);
    
    return protocol;
}

std::vector<int> PreprocessingWorkflow::searchTrialTargets(const ProtocolData& protocol) {
    std::vector<int> trials;
    
    // If protocol is empty, use config defaults
    if (!protocol.isValid()) {
        trials.push_back(config_.ref_trial_id);
        trials.push_back(config_.ref_trial_id + 1);
        return trials;
    }
    
    // Build trial indices based on config filters
    size_t ntrial = protocol.dircond.size();
    bool is_loading = (config_.phase_id == "loading");
    
    for (size_t ii = 0; ii < config_.nfcond_set.size(); ++ii) {
        int nf_set = config_.nfcond_set[ii];
        for (size_t jj = 0; jj < config_.spddxlcond_set.size(); ++jj) {
            double spd_set = config_.spddxlcond_set[jj];
            
            for (size_t i = 1; i < ntrial; ++i) {  // Start from 1 (skip index 0)
                bool pass = true;
                if (is_loading) {
                    bool dir_ok = (protocol.dircond[i] == "Ubnf" || 
                                   protocol.dircond[i] == "Rbnf");
                    bool nf_ok = (i < protocol.nfcond.size()) && 
                                 (protocol.nfcond[i] == nf_set);
                    bool spd_ok = (i < protocol.spdcond.size()) && 
                                  (std::fabs(protocol.spdcond[i] - spd_set) < 1e-9);
                    pass = dir_ok && nf_ok && spd_ok;
                }
                if (pass) {
                    trials.push_back(static_cast<int>(i));
                }
            }
        }
    }
    
    // Remove duplicates
    std::sort(trials.begin(), trials.end());
    trials.erase(std::unique(trials.begin(), trials.end()), trials.end());
    
    return trials;
}

bool PreprocessingWorkflow::validateTrialInputs(int trial_id, int stereopair) {
    // Check video frames exist
    std::string video_path = getTrialVideoPath(trial_id, stereopair);
    if (!std::filesystem::exists(video_path)) {
        return false;
    }
    
    // Check calibration files exist
    std::string calib_path = getCalibrationPath();
    if (!std::filesystem::exists(calib_path)) {
        return false;
    }
    
    return true;
}

TrialInfo PreprocessingWorkflow::getTrialInfo(int trial_id, const ProtocolData& protocol) {
    TrialInfo info;
    info.trial_id = trial_id;
    
    // Format trial as 3-digit string
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << trial_id;
    info.trial_str = oss.str();
    
    // Get protocol data for this trial
    if (trial_id > 0 && static_cast<size_t>(trial_id) < protocol.dircond.size()) {
        info.direction = protocol.dircond[trial_id];
        info.nf = protocol.nfcond[trial_id];
        info.speed = protocol.spdcond[trial_id];
    }
    
    info.reference_trial = determineReferenceTrial(trial_id, protocol);
    
    return info;
}

std::string PreprocessingWorkflow::determineReferenceTrial(int trial_id, 
                                                          const ProtocolData& /*protocol*/) {
    // Use config reference trial if set
    if (config_.ref_trial_id > 0) {
        std::ostringstream oss;
        oss << std::setw(3) << std::setfill('0') << config_.ref_trial_id;
        return oss.str();
    }
    
    // Fallback to current trial
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << trial_id;
    return oss.str();
}

std::string PreprocessingWorkflow::getProtocolPath() const {
    return config_.data_path + "/rawdata/" + config_.subject_id + 
           "/speckles/" + config_.material + "/protocol/";
}

std::string PreprocessingWorkflow::getCalibrationPath() const {
    return config_.data_path + "/rawdata/" + config_.subject_id + 
           "/speckles/" + config_.material + "/calibration/";
}

std::string PreprocessingWorkflow::getTrialOutputPath(int trial_id, 
                                                      const std::string& phase) const {
    std::ostringstream oss;
    oss << config_.dic_path << "/" << config_.subject_id << "/" 
        << config_.material << "/" << std::setw(3) << std::setfill('0') 
        << trial_id << "/" << phase;
    return oss.str();
}

std::string PreprocessingWorkflow::getTrialVideoPath(int trial_id, int stereopair) const {
    // Get camera numbers for this pair
    int cam1 = (stereopair - 1) * 2 + 1;
    int cam2 = (stereopair - 1) * 2 + 2;
    
    std::ostringstream oss;
    oss << config_.data_path << "/rawdata/" << config_.subject_id 
        << "/speckles/" << config_.material << "/videos/";
    
    // Check if directory exists
    std::string base_path = oss.str();
    if (!std::filesystem::exists(base_path)) {
        return "";
    }
    
    // Look for trial-specific subdirectory
    std::ostringstream trial_oss;
    trial_oss << std::setw(3) << std::setfill('0') << trial_id;
    std::string trial_str = trial_oss.str();
    
    for (const auto& entry : std::filesystem::directory_iterator(base_path)) {
        if (entry.is_directory()) {
            std::string dirname = entry.path().filename().string();
            if (dirname.find(trial_str) != std::string::npos) {
                return entry.path().string();
            }
        }
    }
    
    return base_path;
}

} // namespace cppxdic
