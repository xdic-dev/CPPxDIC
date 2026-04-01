#include "cppxdic/io/project_paths.h"

#include "utils.h"
#include <algorithm>

namespace cppxdic::io {

ProjectPaths::ProjectPaths(const Config& config)
    : config_(config) {}

std::filesystem::path ProjectPaths::materialDir() const {
    return std::filesystem::path(Utils::buildOutputUntilMaterialDir(config_));
}

std::filesystem::path ProjectPaths::protocolDir() const {
    return std::filesystem::path(Utils::buildProtocolDir(config_, true, true, true, true));
}

std::filesystem::path ProjectPaths::videoDir() const {
    return std::filesystem::path(Utils::buildVideoDir(config_, true, true, true, true));
}

std::filesystem::path ProjectPaths::calibrationDir() const {
    return std::filesystem::path(Utils::buildCalibDir(config_));
}

std::filesystem::path ProjectPaths::trialDir(int trial) const {
    return std::filesystem::path(Utils::buildOutputUntilTrialDir(config_, trial));
}

std::filesystem::path ProjectPaths::phaseDir(int trial) const {
    return std::filesystem::path(Utils::buildOutputUntilPhaseDir(config_, trial));
}

std::filesystem::path ProjectPaths::dic2DPairResultsFile(int trial, int stereopair, const std::string& extension) const {
    int cam_first = 0;
    int cam_second = 0;
    Utils::getCamerasForPair(stereopair, config_.camera_pairs, cam_first, cam_second);
    return std::filesystem::path(
        Utils::buildDic2DPairResultsFilePath(phaseDir(trial).string(), cam_first, cam_second, extension));
}

std::filesystem::path ProjectPaths::dic3DCombinedFile(int trial, const std::string& extension) const {
    return std::filesystem::path(
        Utils::buildDic3DCombinedFilePath(phaseDir(trial).string(), config_.num_pair, extension));
}

std::filesystem::path ProjectPaths::dic3DPPresultsFile(int trial, const std::string& extension) const {
    return std::filesystem::path(
        Utils::buildDic3DPPresultsFilePath(phaseDir(trial).string(), config_.num_pair, config_.fileversion, extension));
}

std::vector<std::filesystem::path> ProjectPaths::protocolFiles() const {
    std::vector<std::filesystem::path> files;
    for (const auto& file : Utils::findFiles(protocolDir().string(), "*.mat")) {
        files.emplace_back(file);
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace cppxdic::io
