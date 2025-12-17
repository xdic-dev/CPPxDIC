/**
 * Configuration implementation for CPPXDIC
 */

#include "config.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

Config::Config() {
    // Set generate_mat_files based on debug_mode
    if (debug_mode) {
        generate_mat_files = true;
    }
}

void Config::loadGlobalParams() {
    // Set default paths based on current working directory
    setDefaultPaths();
    
    // Initialize derived paths
    data_path = base_path + "/example_data";
    dic_path = base_path + "/analysis";
    
    std::cout << "Global parameters loaded." << std::endl;
}

void Config::loadDicParams() {
    // All parameters are already initialized with default values
    // In a real implementation, these could be loaded from a config file
    
    std::cout << "DIC parameters loaded." << std::endl;
}

void Config::updateVariables() {
    // Update material name from material_id
    if (material_id >= 1 && material_id <= static_cast<int>(frictional_conditions.size())) {
        material = frictional_conditions[material_id - 1];  // Convert to 0-based index
    } else {
        material = "unknown";
        std::cerr << "Warning: Invalid material_id " << material_id << std::endl;
    }
    
    std::cout << "Variables updated. Material: " << material << std::endl;
}

void Config::setDefaultPaths() {
    // Set base path to current working directory or a reasonable default
    if (std::filesystem::exists(base_path)) return;
    try {
        base_path = std::filesystem::current_path().string();
    } catch (const std::exception& e) {
        base_path = ".";
        std::cerr << "Warning: Could not get current path, using '.' as base_path" << std::endl;
    }
}

// Helper function to parse config value from line
std::string Config::parseConfigValue(const std::string& line, const std::string& key) {
    size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) return "";
    
    std::string line_key = line.substr(0, eq_pos);
    // Trim whitespace from key
    line_key.erase(0, line_key.find_first_not_of(" \t"));
    line_key.erase(line_key.find_last_not_of(" \t") + 1);
    
    if (line_key != key) return "";
    
    std::string value = line.substr(eq_pos + 1);
    // Trim whitespace from value
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    
    return value;
}

// Parse comma-separated string list
std::vector<std::string> Config::parseStringList(const std::string& value) {
    std::vector<std::string> result;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // Trim whitespace
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

// Parse comma-separated int list
std::vector<int> Config::parseIntList(const std::string& value) {
    std::vector<int> result;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            result.push_back(std::stoi(item));
        } catch (...) {
            std::cerr << "Warning: Could not parse int value: " << item << std::endl;
        }
    }
    return result;
}

// Parse comma-separated double list
std::vector<double> Config::parseDoubleList(const std::string& value) {
    std::vector<double> result;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            result.push_back(std::stod(item));
        } catch (...) {
            std::cerr << "Warning: Could not parse double value: " << item << std::endl;
        }
    }
    return result;
}

// Parse boolean value
bool Config::parseBool(const std::string& value) {
    std::string lower_value = value;
    std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
    return (lower_value == "true" || lower_value == "1" || lower_value == "yes");
}

// Load DIC parameters from file
bool Config::loadFromDicParamsFile(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        std::cout << "DIC params file not found: " << filepath << ", using defaults" << std::endl;
        return false;
    }
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open DIC params file: " << filepath << std::endl;
        return false;
    }
    
    std::cout << "Loading DIC parameters from: " << filepath << std::endl;
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        // Remove leading/trailing whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.empty() || line[0] == '#') continue;
        
        // Parse each parameter
        std::string value;
        
        // Global parameters
        if (!(value = parseConfigValue(line, "frictional_conditions")).empty()) {
            frictional_conditions = parseStringList(value);
        } else if (!(value = parseConfigValue(line, "num_pair")).empty()) {
            num_pair = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "robot_sample_freq")).empty()) {
            robot_sample_freq = std::stod(value);
        } else if (!(value = parseConfigValue(line, "vid_sample_freq")).empty()) {
            vid_sample_freq = std::stod(value);
        }
        // Path definitions
        else if (!(value = parseConfigValue(line, "base_path")).empty()) {
            base_path = value;
        } else if (!(value = parseConfigValue(line, "data_path")).empty()) {
            if (!value.empty()) data_path = value;
        } else if (!(value = parseConfigValue(line, "dic_path")).empty()) {
            if (!value.empty()) dic_path = value;
        }
        // Processing flags
        else if (!(value = parseConfigValue(line, "im_filter_mode")).empty()) {
            im_filter_mode = parseBool(value);
        } else if (!(value = parseConfigValue(line, "automatic_process")).empty()) {
            automatic_process = parseBool(value);
        } else if (!(value = parseConfigValue(line, "parallel_processing")).empty()) {
            parallel_processing = parseBool(value);
        } else if (!(value = parseConfigValue(line, "debug_mode")).empty()) {
            debug_mode = parseBool(value);
        }
        // DIC analysis parameters
        else if (!(value = parseConfigValue(line, "subject_id")).empty()) {
            subject_id = value;
        } else if (!(value = parseConfigValue(line, "phase_id")).empty()) {
            phase_id = value;
        } else if (!(value = parseConfigValue(line, "material_id")).empty()) {
            material_id = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "nfcond_set")).empty()) {
            nfcond_set = parseIntList(value);
        } else if (!(value = parseConfigValue(line, "spddxlcond_set")).empty()) {
            spddxlcond_set = parseDoubleList(value);
        } else if (!(value = parseConfigValue(line, "calib_folder_set")).empty()) {
            calib_folder_set = value;
        } else if (!(value = parseConfigValue(line, "ref_trial_id")).empty()) {
            ref_trial_id = std::stoi(value);
        }
        // Frame settings
        else if (!(value = parseConfigValue(line, "idx_frame_start")).empty()) {
            idx_frame_start = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "idx_frame_end")).empty()) {
            idx_frame_end = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "frame_jump")).empty()) {
            frame_jump = std::stoi(value);
        }
        // Units and calibration
        else if (!(value = parseConfigValue(line, "units_per_pixel")).empty()) {
            units_per_pixel = std::stod(value);
        } else if (!(value = parseConfigValue(line, "subregion_radius")).empty()) {
            subregion_radius = std::stoi(value);
        }
        // Output control
        else if (!(value = parseConfigValue(line, "fileversion")).empty()) {
            fileversion = value;
        } else if (!(value = parseConfigValue(line, "generate_mat_files")).empty()) {
            generate_mat_files = parseBool(value);
        } else if (!(value = parseConfigValue(line, "cleanup_cache_bins")).empty()) {
            cleanup_cache_bins = parseBool(value);
        }
    }
    
    file.close();
    std::cout << "DIC parameters loaded from file" << std::endl;
    return true;
}

// Load NCorr parameters from file (overrides DIC params where applicable)
bool Config::loadFromNcorrParamsFile(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        std::cout << "NCorr params file not found: " << filepath << ", skipping" << std::endl;
        return false;
    }
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open NCorr params file: " << filepath << std::endl;
        return false;
    }
    
    std::cout << "Loading NCorr parameters from: " << filepath << std::endl;
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.empty() || line[0] == '#') continue;
        
        std::string value;
        
        // Override units_per_pixel if present
        if (!(value = parseConfigValue(line, "units_per_pixel")).empty()) {
            units_per_pixel = std::stod(value);
        } else if (!(value = parseConfigValue(line, "radius")).empty()) {
            subregion_radius = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "debug")).empty()) {
            debug_mode = parseBool(value);
        } else if (!(value = parseConfigValue(line, "threads")).empty()) {
            // Could store this for later use in step parameters
        } else if (!(value = parseConfigValue(line, "maxCorrCoeff")).empty()) {
            maxCorrCoeff = std::stod(value);
        }
    }
    
    file.close();
    std::cout << "NCorr parameters loaded from file" << std::endl;
    return true;
}

// Load visualization parameters from file
bool Config::loadFromVisualizationParamsFile(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        std::cout << "Visualization params file not found: " << filepath << ", using defaults" << std::endl;
        return false;
    }
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open visualization params file: " << filepath << std::endl;
        return false;
    }
    
    std::cout << "Loading visualization parameters from: " << filepath << std::endl;
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.empty() || line[0] == '#') continue;
        
        std::string value;
        
        // Visualization settings
        if (!(value = parseConfigValue(line, "showvisu")).empty()) {
            showvisu = parseBool(value);
        } else if (!(value = parseConfigValue(line, "mapLogic")).empty()) {
            mapLogic = parseBool(value);
        } else if (!(value = parseConfigValue(line, "plotopt")).empty()) {
            plotopt = parseStringList(value);
        } else if (!(value = parseConfigValue(line, "deftype")).empty()) {
            deftype = value;
        } else if (!(value = parseConfigValue(line, "viewplot")).empty()) {
            viewplot = value;
        } else if (!(value = parseConfigValue(line, "contactAreaLogic")).empty()) {
            contactAreaLogic = parseBool(value);
        } else if (!(value = parseConfigValue(line, "gapLogic")).empty()) {
            gapLogic = parseBool(value);
        } else if (!(value = parseConfigValue(line, "maxCorrCoeff")).empty()) {
            maxCorrCoeff = std::stod(value);
        } else if (!(value = parseConfigValue(line, "format")).empty()) {
            format = value;
        }
        // Filtering settings
        else if (!(value = parseConfigValue(line, "smoothTimeLogic")).empty()) {
            smoothTimeLogic = parseBool(value);
        } else if (!(value = parseConfigValue(line, "filterFreq")).empty()) {
            filterFreq = std::stod(value);
        } else if (!(value = parseConfigValue(line, "smoothSpaceLogic")).empty()) {
            smoothSpaceLogic = parseBool(value);
        } else if (!(value = parseConfigValue(line, "smoothPar_n")).empty()) {
            smoothPar_n = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "smoothPar_sigma")).empty()) {
            smoothPar_sigma = std::stod(value);
        }
        // Additional plot settings
        else if (!(value = parseConfigValue(line, "showRobotLogic")).empty()) {
            showRobotLogic = parseBool(value);
        } else if (!(value = parseConfigValue(line, "showImgLogic")).empty()) {
            showImgLogic = parseBool(value);
        } else if (!(value = parseConfigValue(line, "camNbr")).empty()) {
            camNbr = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "tracesLogic")).empty()) {
            tracesLogic = parseBool(value);
        }
        // Plot appearance
        else if (!(value = parseConfigValue(line, "FaceAlpha")).empty()) {
            FaceAlpha = std::stod(value);
        } else if (!(value = parseConfigValue(line, "lineColor")).empty()) {
            lineColor = value;
        } else if (!(value = parseConfigValue(line, "quiverScaleFactor")).empty()) {
            quiverScaleFactor = std::stod(value);
        }
        // Export settings
        else if (!(value = parseConfigValue(line, "export_format")).empty()) {
            export_format = value;
        } else if (!(value = parseConfigValue(line, "export_each_frame")).empty()) {
            export_each_frame = parseBool(value);
        } else if (!(value = parseConfigValue(line, "export_frame_list")).empty()) {
            export_frame_list = parseIntList(value);
        }
    }
    
    file.close();
    std::cout << "Visualization parameters loaded from file" << std::endl;
    return true;
}

// Override subject from command line
void Config::overrideSubject(const std::string& subject) {
    subject_id = subject;
    std::cout << "Subject overridden to: " << subject_id << std::endl;
}

// Override reference trial from command line
void Config::overrideRefTrial(int trial) {
    ref_trial_id = trial;
    std::cout << "Reference trial overridden to: " << ref_trial_id << std::endl;
}
