#ifndef CPPXDIC_IO_PROJECT_PATHS_H
#define CPPXDIC_IO_PROJECT_PATHS_H

#include "config.h"
#include <filesystem>
#include <string>
#include <vector>

namespace cppxdic::io {

class ProjectPaths {
public:
    explicit ProjectPaths(const Config& config);

    std::filesystem::path materialDir() const;
    std::filesystem::path protocolDir() const;
    std::filesystem::path videoDir() const;
    std::filesystem::path calibrationDir() const;
    std::filesystem::path trialDir(int trial) const;
    std::filesystem::path phaseDir(int trial) const;

    std::filesystem::path dic2DPairResultsFile(int trial, int stereopair, const std::string& extension) const;
    std::filesystem::path dic3DCombinedFile(int trial, const std::string& extension) const;
    std::filesystem::path dic3DPPresultsFile(int trial, const std::string& extension) const;

    std::vector<std::filesystem::path> protocolFiles() const;

private:
    const Config& config_;
};

} // namespace cppxdic::io

#endif // CPPXDIC_IO_PROJECT_PATHS_H
