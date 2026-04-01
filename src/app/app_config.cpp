#include "cppxdic/app/app_config.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace cppxdic::app {

AppConfig AppConfig::load(const CliOptions& options) {
    AppConfig app_config;
    app_config.input_paths_.dic_params_file = options.dic_params_file;
    app_config.input_paths_.ncorr_params_file = options.ncorr_params_file;
    app_config.input_paths_.visualization_params_file = options.viz_params_file;

    std::cout << "Loading configuration files..." << std::endl;
    std::cout << "------------------------------" << std::endl;

    app_config.legacy_config_.loadFromDicParamsFile(app_config.input_paths_.dic_params_file.string());
    app_config.legacy_config_.loadFromNcorrParamsFile(app_config.input_paths_.ncorr_params_file.string());
    app_config.legacy_config_.loadFromVisualizationParamsFile(app_config.input_paths_.visualization_params_file.string());

    if (!options.subject_override.empty()) {
        app_config.legacy_config_.overrideSubject(options.subject_override);
    }
    if (options.reftrial_override >= 0) {
        app_config.legacy_config_.overrideRefTrial(options.reftrial_override);
    }

    app_config.legacy_config_.updateVariables();

    app_config.step_d_.dic = app_config.legacy_config_.step_d;
    app_config.step_d_.replace_bad_correlation = app_config.legacy_config_.step_d_replacebadcorr;

    app_config.step_e_.dic = app_config.legacy_config_.step_e;
    app_config.step_e_.distortion_removal = app_config.legacy_config_.step_e_distortion_removal;

    app_config.step_f_.dic = app_config.legacy_config_.step_f;
    app_config.step_f_.temporal_filtering = app_config.legacy_config_.step_f_temporal_filtering;
    app_config.step_f_.temporal_filter_frequency = app_config.legacy_config_.step_f_freq_filt;
    app_config.step_f_.compute_rbm = app_config.legacy_config_.step_f_compute_rbm;

    app_config.validate();
    return app_config;
}

void AppConfig::printSummary(std::ostream& os) const {
    os << '\n';
    os << "Final Configuration:" << std::endl;
    os << "--------------------" << std::endl;
    os << "Subject: " << legacy_config_.subject_id
       << ", Phase: " << legacy_config_.phase_id
       << ", Material: " << legacy_config_.material
       << ", Stereo Pairs: " << legacy_config_.num_pair << std::endl;
    os << "Reference trial number: " << legacy_config_.ref_trial_id << std::endl;
    os << "Frame: " << legacy_config_.idx_frame_start
       << " to " << legacy_config_.idx_frame_end
       << ", jump= " << legacy_config_.frame_jump << std::endl;
    os << "Show visualization: " << legacy_config_.showvisu
       << ", Debug mode: " << legacy_config_.debug_mode
       << ", Automatic process: " << legacy_config_.automatic_process << std::endl;
    os << "Data format: " << legacy_config_.data_format
       << ", Step F temporal filtering: " << step_f_.temporal_filtering
       << " @ " << step_f_.temporal_filter_frequency << " Hz" << std::endl;
    os << std::endl;
}

void AppConfig::validate() const {
    if (legacy_config_.subject_id.empty()) {
        throw std::invalid_argument("subject_id must not be empty");
    }
    if (legacy_config_.num_pair <= 0) {
        throw std::invalid_argument("num_pair must be positive");
    }
    if (legacy_config_.idx_frame_end > 0 &&
        legacy_config_.idx_frame_end < legacy_config_.idx_frame_start) {
        throw std::invalid_argument("idx_frame_end must be >= idx_frame_start");
    }

    static const std::vector<std::string> valid_formats = {"mat", "bin", "json"};
    if (std::find(valid_formats.begin(), valid_formats.end(), legacy_config_.data_format) == valid_formats.end()) {
        throw std::invalid_argument("Unsupported data_format: " + legacy_config_.data_format);
    }
}

} // namespace cppxdic::app
