#include "cppxdic/pipeline/checkpoint_policy.h"

#include <filesystem>

namespace cppxdic::pipeline {

CheckpointPolicy::CheckpointPolicy(const Config& config, const io::ProjectPaths& paths)
    : config_(config), paths_(paths) {}

bool CheckpointPolicy::hasStepDOutputs(const std::vector<int>& trials, const cppxdic::DataSerializer& serializer) const {
    const std::string extension = serializer.extension();
    for (const int trial : trials) {
        for (int stereopair = 1; stereopair <= config_.num_pair; ++stereopair) {
            if (!std::filesystem::exists(paths_.dic2DPairResultsFile(trial, stereopair, extension))) {
                return false;
            }
        }
    }
    return true;
}

bool CheckpointPolicy::hasStepEOutputs(const std::vector<int>& trials, const cppxdic::DataSerializer& serializer) const {
    const std::string extension = serializer.extension();
    for (const int trial : trials) {
        if (!std::filesystem::exists(paths_.dic3DCombinedFile(trial, extension))) {
            return false;
        }
    }
    return true;
}

bool CheckpointPolicy::hasStepFOutputs(const std::vector<int>& trials, const cppxdic::DataSerializer& serializer) const {
    const std::string extension = serializer.extension();
    for (const int trial : trials) {
        if (!std::filesystem::exists(paths_.dic3DPPresultsFile(trial, extension))) {
            return false;
        }
    }
    return true;
}

} // namespace cppxdic::pipeline
