#ifndef CPPXDIC_APP_APP_CONFIG_H
#define CPPXDIC_APP_APP_CONFIG_H

#include "config.h"
#include "cppxdic/app/cli.h"
#include <filesystem>
#include <ostream>
#include <string>

namespace cppxdic::app {

struct InputPaths {
    std::filesystem::path dic_params_file = "dic_params.txt";
    std::filesystem::path ncorr_params_file = "ncorr_params.txt";
    std::filesystem::path visualization_params_file = "visualization_params.txt";
};

struct StepDConfig {
    StepConfig dic;
    bool replace_bad_correlation = true;
};

struct StepEConfig {
    StepConfig dic;
    bool distortion_removal = false;
};

struct StepFConfig {
    StepConfig dic;
    bool temporal_filtering = true;
    double temporal_filter_frequency = 10.0;
    bool compute_rbm = false;
};

class AppConfig {
public:
    static AppConfig load(const CliOptions& options);

    const Config& legacy() const noexcept { return legacy_config_; }
    Config& legacy() noexcept { return legacy_config_; }

    const InputPaths& inputPaths() const noexcept { return input_paths_; }
    const StepDConfig& stepD() const noexcept { return step_d_; }
    const StepEConfig& stepE() const noexcept { return step_e_; }
    const StepFConfig& stepF() const noexcept { return step_f_; }

    void printSummary(std::ostream& os) const;

private:
    void validate() const;

    Config legacy_config_;
    InputPaths input_paths_;
    StepDConfig step_d_;
    StepEConfig step_e_;
    StepFConfig step_f_;
};

} // namespace cppxdic::app

#endif // CPPXDIC_APP_APP_CONFIG_H
