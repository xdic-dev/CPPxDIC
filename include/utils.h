/**
 * Utility functions for CPPXDIC
 * Equivalent to various utility functions in Matlab xDIC
 */

#ifndef UTILS_H
#define UTILS_H

#include "config.h"
#include <string>
#include <vector>

class Utils {
public:
    // Check data and protocol files (equivalent to dic_check.m)
    static bool dicCheck(const Config& config);
    
    // File system utilities
    static bool fileExists(const std::string& path);
    static bool directoryExists(const std::string& path);
    static std::vector<std::string> findFiles(const std::string& directory, 
                                            const std::string& pattern);
    
    // String utilities
    static std::string formatString(const std::string& format, ...);
    static std::vector<std::string> split(const std::string& str, char delimiter);

    // Video import (equivalents of import_vid.m / import_raw_vid.m)
    static bool importVid(const Config& config,
                          int trial,
                          int stereopair,
                          std::vector<std::string>& cam1Frames,
                          std::vector<std::string>& cam2Frames);

    static bool importRawVid(const Config& config,
                             int trial,
                             int stereopair,
                             int frameStart,
                             int frameEnd,
                             int frameJump,
                             std::vector<std::string>& cam1Frames,
                             std::vector<std::string>& cam2Frames);

    // ROI/SEED from MAT to JSON
    static bool loadROIFromMat(const Config& config,
                               int trial,
                               int stereopair,
                               std::string& roiJsonPath,
                               std::string& roiMaskImagePath);

    static bool loadSeedFromMat(const Config& config,
                                int trial,
                                int stereopair,
                                std::string& seedJsonPath);
    
private:
    static bool checkROIReferences(const Config& config);
    static bool checkSeedReferences(const Config& config);
    static bool checkProtocolFiles(const Config& config);
    static bool checkCalibrationFiles(const Config& config);

    // Helpers for video
    static void getCamerasForPair(int stereopair, int& cam_first, int& cam_second);
};

#endif // UTILS_H
