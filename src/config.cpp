/**
 * Configuration implementation for CPPXDIC
 */

#include "config.h"
#include "logging.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

Config::Config() {
    // Set step_e defaults (matching uses larger radius)
    step_e.radius = 60;
}

void Config::updateVariables() {
    // Set default paths if not explicitly set from config file
    setDefaultPaths();
    if (data_path.empty()) {
        data_path = base_path + "/example_data";
    }
    if (dic_path.empty()) {
        dic_path = base_path + "/analysis";
    }

    // Update material name from material_id
    if (material_id >= 1 && material_id <= static_cast<int>(frictional_conditions.size())) {
        material = frictional_conditions[material_id - 1]; // Convert to 0-based index
    } else {
        material = "unknown";
        LOG_WARN << "Invalid material_id " << material_id;
    }

    LOG_INFO << "Variables updated. Material: " << material;
}

void Config::setDefaultPaths() {
    // Set base path to current working directory or a reasonable default
    if (std::filesystem::exists(base_path)) return;
    try {
        base_path = std::filesystem::current_path().string();
    } catch (const std::exception& e) {
        base_path = ".";
        LOG_WARN << "Could not get current path, using '.' as base_path";
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
            LOG_WARN << "Could not parse int value: " << item;
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
            LOG_WARN << "Could not parse double value: " << item;
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
        LOG_INFO << "DIC params file not found: " << filepath << ", using defaults";
        return false;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_WARN << "Could not open DIC params file: " << filepath;
        return false;
    }

    LOG_INFO << "Loading DIC parameters from: " << filepath;

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
        } else if (!(value = parseConfigValue(line, "log_level")).empty()) {
            log_level = value;
        } else if (!(value = parseConfigValue(line, "log_file")).empty()) {
            log_file = value;
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
        // Units and calibration
        else if (!(value = parseConfigValue(line, "limit_grayscale")).empty()) {
            limit_grayscale = std::stoi(value);
        }
        // Output control
        else if (!(value = parseConfigValue(line, "fileversion")).empty()) {
            fileversion = value;
        } else if (!(value = parseConfigValue(line, "data_format")).empty()) {
            data_format = value;
        }
        // Step D parameters
        else if (!(value = parseConfigValue(line, "step_d_analysis_type")).empty()) {
            step_d.analysis_type = value;
        } else if (!(value = parseConfigValue(line, "step_d_radius")).empty()) {
            step_d.radius = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_d_spacing")).empty()) {
            step_d.spacing = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_d_cutoff_diffnorm")).empty()) {
            step_d.cutoff_diffnorm = std::stod(value);
        } else if (!(value = parseConfigValue(line, "step_d_cutoff_iteration")).empty()) {
            step_d.cutoff_iteration = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_d_total_threads")).empty()) {
            step_d.total_threads = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_d_high_strain_enabled")).empty()) {
            step_d.high_strain_enabled = parseBool(value);
        } else if (!(value = parseConfigValue(line, "step_d_seed_type")).empty()) {
            step_d.seed_type = value;
        } else if (!(value = parseConfigValue(line, "step_d_auto_update")).empty()) {
            step_d.auto_update = parseBool(value);
        } else if (!(value = parseConfigValue(line, "step_d_step_ref_change")).empty()) {
            step_d.step_ref_change = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_d_initial_seed")).empty()) {
            step_d.initial_seed = parseIntList(value);
        }
        // Step E parameters
        else if (!(value = parseConfigValue(line, "step_e_analysis_type")).empty()) {
            step_e.analysis_type = value;
        } else if (!(value = parseConfigValue(line, "step_e_radius")).empty()) {
            step_e.radius = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_e_spacing")).empty()) {
            step_e.spacing = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_e_cutoff_diffnorm")).empty()) {
            step_e.cutoff_diffnorm = std::stod(value);
        } else if (!(value = parseConfigValue(line, "step_e_cutoff_iteration")).empty()) {
            step_e.cutoff_iteration = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_e_total_threads")).empty()) {
            step_e.total_threads = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_e_high_strain_enabled")).empty()) {
            step_e.high_strain_enabled = parseBool(value);
        } else if (!(value = parseConfigValue(line, "step_e_seed_type")).empty()) {
            step_e.seed_type = value;
        } else if (!(value = parseConfigValue(line, "step_e_auto_update")).empty()) {
            step_e.auto_update = parseBool(value);
        } else if (!(value = parseConfigValue(line, "step_e_step_ref_change")).empty()) {
            step_e.step_ref_change = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_e_initial_seed")).empty()) {
            step_e.initial_seed = parseIntList(value);
        }
        // Step F parameters
        else if (!(value = parseConfigValue(line, "step_f_analysis_type")).empty()) {
            step_f.analysis_type = value;
        } else if (!(value = parseConfigValue(line, "step_f_radius")).empty()) {
            step_f.radius = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_f_spacing")).empty()) {
            step_f.spacing = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_f_cutoff_diffnorm")).empty()) {
            step_f.cutoff_diffnorm = std::stod(value);
        } else if (!(value = parseConfigValue(line, "step_f_cutoff_iteration")).empty()) {
            step_f.cutoff_iteration = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_f_total_threads")).empty()) {
            step_f.total_threads = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_f_high_strain_enabled")).empty()) {
            step_f.high_strain_enabled = parseBool(value);
        } else if (!(value = parseConfigValue(line, "step_f_seed_type")).empty()) {
            step_f.seed_type = value;
        } else if (!(value = parseConfigValue(line, "step_f_auto_update")).empty()) {
            step_f.auto_update = parseBool(value);
        } else if (!(value = parseConfigValue(line, "step_f_step_ref_change")).empty()) {
            step_f.step_ref_change = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "step_f_initial_seed")).empty()) {
            step_f.initial_seed = parseIntList(value);
        }
        // Step E specific parameters (3D reconstruction)
        else if (!(value = parseConfigValue(line, "step_e_distortion_removal")).empty()) {
            step_e_distortion_removal = parseBool(value);
        } else if (!(value = parseConfigValue(line, "step_d_replacebadcorr")).empty()) {
            step_d_replacebadcorr = parseBool(value);
        }
        // Camera pairs mapping (format: "1,2;4,3" for pair1=(1,2), pair2=(4,3))
        else if (!(value = parseConfigValue(line, "camera_pairs")).empty()) {
            camera_pairs.clear();
            // Parse semicolon-separated pairs, each pair is "cam1,cam2"
            std::istringstream pairs_stream(value);
            std::string pair_str;
            while (std::getline(pairs_stream, pair_str, ';')) {
                auto comma_pos = pair_str.find(',');
                if (comma_pos != std::string::npos) {
                    int c1 = std::stoi(pair_str.substr(0, comma_pos));
                    int c2 = std::stoi(pair_str.substr(comma_pos + 1));
                    camera_pairs.push_back({c1, c2});
                }
            }
        }
        // Step F specific parameters (deformation analysis)
        else if (!(value = parseConfigValue(line, "step_f_temporal_filtering")).empty()) {
            step_f_temporal_filtering = parseBool(value);
        } else if (!(value = parseConfigValue(line, "step_f_freq_filt")).empty()) {
            step_f_freq_filt = std::stod(value);
        } else if (!(value = parseConfigValue(line, "step_f_compute_rbm")).empty()) {
            step_f_compute_rbm = parseBool(value);
        }
    }

    file.close();
    LOG_INFO << "DIC parameters loaded from file";
    return true;
}

// Load NCorr parameters from file (overrides DIC params where applicable)
bool Config::loadFromNcorrParamsFile(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        LOG_INFO << "NCorr params file not found: " << filepath << ", skipping";
        return false;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_WARN << "Could not open NCorr params file: " << filepath;
        return false;
    }

    LOG_INFO << "Loading NCorr parameters from: " << filepath;

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        line.erase(0, line.find_first_not_of(" \t"));
        if (line.empty() || line[0] == '#') continue;

        std::string value;

        if (!(value = parseConfigValue(line, "units_per_pixel")).empty()) {
            units_per_pixel = std::stod(value);
        } else if (!(value = parseConfigValue(line, "radius")).empty()) {
            subregion_radius = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "debug")).empty()) {
            debug_mode = parseBool(value);
        } else if (!(value = parseConfigValue(line, "threads")).empty()) {
            ncorr_threads = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "maxCorrCoeff")).empty()) {
            maxCorrCoeff = std::stod(value);
        } else if (!(value = parseConfigValue(line, "scalefactor")).empty()) {
            ncorr_scalefactor = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "interp")).empty()) {
            ncorr_interp = value;
        } else if (!(value = parseConfigValue(line, "subregion")).empty()) {
            ncorr_subregion = value;
        } else if (!(value = parseConfigValue(line, "dic_config")).empty()) {
            ncorr_dic_config = value;
        } else if (!(value = parseConfigValue(line, "cutoff_corrcoef")).empty()) {
            ncorr_cutoff_corrcoef = std::stod(value);
        } else if (!(value = parseConfigValue(line, "roi_update_mode")).empty()) {
            ncorr_roi_update_mode = value;
        } else if (!(value = parseConfigValue(line, "accumulation_mode")).empty()) {
            ncorr_accumulation_mode = value;
        } else if (!(value = parseConfigValue(line, "save_disps_steps")).empty()) {
            ncorr_save_disps_steps = parseBool(value);
        } else if (!(value = parseConfigValue(line, "perspective_interp")).empty()) {
            ncorr_perspective_interp = parseBool(value);
        } else if (!(value = parseConfigValue(line, "units")).empty()) {
            ncorr_units = value;
        } else if (!(value = parseConfigValue(line, "seeds_are_optimized")).empty()) {
            ncorr_seeds_are_optimized = parseBool(value);
        } else if (!(value = parseConfigValue(line, "cutoff_max_diffnorm")).empty()) {
            ncorr_cutoff_max_diffnorm = std::stod(value);
        } else if (!(value = parseConfigValue(line, "cutoff_max_corrcoef")).empty()) {
            ncorr_cutoff_max_corrcoef = std::stod(value);
        } else if (!(value = parseConfigValue(line, "use_exact_matlab")).empty()) {
            ncorr_use_exact_matlab = parseBool(value);
        } else if (!(value = parseConfigValue(line, "dic_engine")).empty()) {
            dic_engine = value;
        } else if (!(value = parseConfigValue(line, "cuncorr_seed_search")).empty()) {
            cuncorr_seed_search = std::stoi(value);
        }
    }

    file.close();
    LOG_INFO << "NCorr parameters loaded from file";
    return true;
}

// Load visualization parameters from file
bool Config::loadFromVisualizationParamsFile(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        LOG_INFO << "Visualization params file not found: " << filepath << ", using defaults";
        return false;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_WARN << "Could not open visualization params file: " << filepath;
        return false;
    }

    LOG_INFO << "Loading visualization parameters from: " << filepath;

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
        // VTK export options
        else if (!(value = parseConfigValue(line, "vtk_format")).empty()) {
            vtk_format = value;
        } else if (!(value = parseConfigValue(line, "vtk_include_scalars")).empty()) {
            vtk_include_scalars = parseBool(value);
        } else if (!(value = parseConfigValue(line, "vtk_include_vectors")).empty()) {
            vtk_include_vectors = parseBool(value);
        }
        // PLY export options
        else if (!(value = parseConfigValue(line, "ply_format")).empty()) {
            ply_format = value;
        } else if (!(value = parseConfigValue(line, "ply_include_colors")).empty()) {
            ply_include_colors = parseBool(value);
        }
        // CSV export options
        else if (!(value = parseConfigValue(line, "csv_delimiter")).empty()) {
            csv_delimiter = value;
        } else if (!(value = parseConfigValue(line, "csv_include_header")).empty()) {
            csv_include_header = parseBool(value);
        }
        // Video options
        else if (!(value = parseConfigValue(line, "generate_videos")).empty()) {
            generate_videos = parseBool(value);
        } else if (!(value = parseConfigValue(line, "video_fps")).empty()) {
            video_fps = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "video_codec")).empty()) {
            video_codec = value;
        } else if (!(value = parseConfigValue(line, "video_quality")).empty()) {
            video_quality = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "video_alpha")).empty()) {
            video_alpha = std::stod(value);
        }
        // Colormap options
        else if (!(value = parseConfigValue(line, "colormap")).empty()) {
            colormap = value;
        } else if (!(value = parseConfigValue(line, "colormap_range_mode")).empty()) {
            colormap_range_mode = value;
        } else if (!(value = parseConfigValue(line, "colormap_min")).empty()) {
            colormap_min = std::stod(value);
        } else if (!(value = parseConfigValue(line, "colormap_max")).empty()) {
            colormap_max = std::stod(value);
        } else if (!(value = parseConfigValue(line, "colormap_levels")).empty()) {
            colormap_levels = std::stoi(value);
        }
        // Statistics options
        else if (!(value = parseConfigValue(line, "generate_summary_stats")).empty()) {
            generate_summary_stats = parseBool(value);
        } else if (!(value = parseConfigValue(line, "stats_format")).empty()) {
            stats_format = value;
        } else if (!(value = parseConfigValue(line, "stats_per_frame")).empty()) {
            stats_per_frame = parseBool(value);
        } else if (!(value = parseConfigValue(line, "stats_spatial")).empty()) {
            stats_spatial = parseBool(value);
        } else if (!(value = parseConfigValue(line, "stats_temporal")).empty()) {
            stats_temporal = parseBool(value);
        }
        // Advanced visualization options
        else if (!(value = parseConfigValue(line, "mesh_decimation")).empty()) {
            mesh_decimation = std::stod(value);
        } else if (!(value = parseConfigValue(line, "mesh_smoothing")).empty()) {
            mesh_smoothing = parseBool(value);
        } else if (!(value = parseConfigValue(line, "mesh_smoothing_iterations")).empty()) {
            mesh_smoothing_iterations = std::stoi(value);
        } else if (!(value = parseConfigValue(line, "show_axes")).empty()) {
            show_axes = parseBool(value);
        } else if (!(value = parseConfigValue(line, "show_colorbar")).empty()) {
            show_colorbar = parseBool(value);
        } else if (!(value = parseConfigValue(line, "show_grid")).empty()) {
            show_grid = parseBool(value);
        } else if (!(value = parseConfigValue(line, "background_color")).empty()) {
            background_color = value;
        }
    }

    file.close();
    LOG_INFO << "Visualization parameters loaded from file";
    return true;
}

// Load a single unified config file (config-file tier of the override chain).
// Reuses the three existing per-file loaders; each only reacts to its own keys, so a
// unified file containing any mix of DIC / NCorr / visualization keys is parsed correctly.
bool Config::loadFromConfigFile(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        LOG_INFO << "Config file not found: " << filepath
                 << ", using compiled defaults / per-file params";
        return false;
    }
    LOG_INFO << "Loading unified config file: " << filepath;
    // Order does not matter: the loaders key on disjoint parameter names.
    loadFromDicParamsFile(filepath);
    loadFromNcorrParamsFile(filepath);
    loadFromVisualizationParamsFile(filepath);
    return true;
}

// Override subject from command line
void Config::overrideSubject(const std::string& subject) {
    subject_id = subject;
    LOG_INFO << "Subject overridden to: " << subject_id;
}

// Override reference trial from command line
void Config::overrideRefTrial(int trial) {
    ref_trial_id = trial;
    LOG_INFO << "Reference trial overridden to: " << ref_trial_id;
}
