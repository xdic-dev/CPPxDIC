#include "cppxdic/pipeline/trial_selector.h"

#include "cppxdic/io/mat/mat_reader.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <iostream>

namespace cppxdic::pipeline {

namespace {

int subjectNumber(const std::string& subject_id) {
    int value = 0;
    for (const char ch : subject_id) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            value = value * 10 + (ch - '0');
        }
    }
    return value;
}

} // namespace

TrialSelector::TrialSelector(const Config& config)
    : config_(config), paths_(nullptr) {}

TrialSelector::TrialSelector(const Config& config, const io::ProjectPaths& paths)
    : config_(config), paths_(&paths) {}

std::vector<int> TrialSelector::fallbackTrials(const Config& config) {
    return {config.ref_trial_id, config.ref_trial_id + 1};
}

std::vector<int> TrialSelector::selectTargets() const {
    io::ProjectPaths owned_paths(config_);
    const io::ProjectPaths& paths = paths_ ? *paths_ : owned_paths;

    const auto protocol_files = paths.protocolFiles();
    if (protocol_files.empty()) {
        std::cerr << "Protocol file not found in: " << paths.protocolDir() << std::endl;
        return fallbackTrials(config_);
    }

    cppxdic::ProtocolFileData protocol;
    if (!cppxdic::io::mat::Reader::loadProtocol(protocol_files.front().string(), protocol)) {
        std::cerr << "Failed to load protocol file: " << protocol_files.front() << std::endl;
        return fallbackTrials(config_);
    }
    if (protocol.trials.empty()) {
        std::cerr << "Protocol file did not contain any trials: " << protocol_files.front() << std::endl;
        return fallbackTrials(config_);
    }

    std::vector<double> speeds;
    speeds.reserve(protocol.trials.size());
    for (const auto& trial : protocol.trials) {
        speeds.push_back(trial.speed);
    }

    if (subjectNumber(config_.subject_id) < 8 && protocol.trials.size() > 1) {
        const size_t half = protocol.trials.size() / 2;
        for (size_t index = 0; index < protocol.trials.size(); ++index) {
            speeds[index] = (index < half) ? 0.04 : 0.08;
        }
    }

    std::vector<int> selected_trials;
    const bool is_loading = (config_.phase_id == "loading");

    if (!is_loading) {
        for (const auto& trial : protocol.trials) {
            selected_trials.push_back(trial.trial_number);
        }
    } else {
        for (const int nf_target : config_.nfcond_set) {
            for (const double speed_target : config_.spddxlcond_set) {
                for (size_t index = 0; index < protocol.trials.size(); ++index) {
                    const auto& trial = protocol.trials[index];
                    const bool direction_matches = (trial.direction == "Ubnf" || trial.direction == "Rbnf");
                    const bool force_matches = std::fabs(trial.force - nf_target) < 1e-6;
                    const bool speed_matches = std::fabs(speeds[index] - speed_target) < 1e-9;
                    if (direction_matches && force_matches && speed_matches) {
                        selected_trials.push_back(trial.trial_number);
                    }
                }
            }
        }
    }

    std::sort(selected_trials.begin(), selected_trials.end());
    selected_trials.erase(std::unique(selected_trials.begin(), selected_trials.end()), selected_trials.end());

    if (selected_trials.empty()) {
        return fallbackTrials(config_);
    }
    return selected_trials;
}

} // namespace cppxdic::pipeline
