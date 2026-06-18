/**
 * Implementation of binary serialization for DIC structures
 */

#include "dic_structures.h"
#include "logging.h"
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace cppxdic {

// Helper functions for binary I/O
template<typename T>
void writePOD(std::ofstream& ofs, const T& value) {
    ofs.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template<typename T>
void readPOD(std::ifstream& ifs, T& value) {
    ifs.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template<typename T>
void writeVector(std::ofstream& ofs, const std::vector<T>& vec) {
    size_t size = vec.size();
    writePOD(ofs, size);
    if (size > 0) {
        ofs.write(reinterpret_cast<const char*>(vec.data()), static_cast<std::streamsize>(size * sizeof(T)));
    }
}

template<typename T>
void readVector(std::ifstream& ifs, std::vector<T>& vec) {
    size_t size;
    readPOD(ifs, size);
    vec.resize(size);
    if (size > 0) {
        ifs.read(reinterpret_cast<char*>(vec.data()), static_cast<std::streamsize>(size * sizeof(T)));
    }
}

void writeString(std::ofstream& ofs, const std::string& str) {
    size_t size = str.size();
    writePOD(ofs, size);
    if (size > 0) {
        ofs.write(str.data(), static_cast<std::streamsize>(size));
    }
}

void readString(std::ifstream& ifs, std::string& str) {
    size_t size;
    readPOD(ifs, size);
    str.resize(size);
    if (size > 0) {
        ifs.read(&str[0], static_cast<std::streamsize>(size));
    }
}

template<typename T>
void writeVector2D(std::ofstream& ofs, const std::vector<std::vector<T>>& vec) {
    size_t outer_size = vec.size();
    writePOD(ofs, outer_size);
    for (const auto& inner : vec) {
        writeVector(ofs, inner);
    }
}

template<typename T>
void readVector2D(std::ifstream& ifs, std::vector<std::vector<T>>& vec) {
    size_t outer_size;
    readPOD(ifs, outer_size);
    vec.resize(outer_size);
    for (auto& inner : vec) {
        readVector(ifs, inner);
    }
}

void writePoints3D(std::ofstream& ofs, const Point3DFrame& pts) {
    writeVector(ofs, pts.x);
    writeVector(ofs, pts.y);
    writeVector(ofs, pts.z);
}

void readPoints3D(std::ifstream& ifs, Point3DFrame& pts) {
    readVector(ifs, pts.x);
    readVector(ifs, pts.y);
    readVector(ifs, pts.z);
}

void writeDispData(std::ofstream& ofs, const DispData& disp) {
    writeVector2D(ofs, disp.DispVec);
    writeVector2D(ofs, disp.DispMgn);
}

void readDispData(std::ifstream& ifs, DispData& disp) {
    readVector2D(ifs, disp.DispVec);
    readVector2D(ifs, disp.DispMgn);
}

void writeCalibrationData(std::ofstream& ofs, const CalibrationData& calib) {
    // Write DLT_paths (2D array of strings)
    size_t rows = calib.DLT_paths.size();
    writePOD(ofs, rows);
    for (const auto& row : calib.DLT_paths) {
        size_t cols = row.size();
        writePOD(ofs, cols);
        for (const auto& path : row) {
            writeString(ofs, path);
        }
    }
    
    // Write DLT_params (3D array: rows x cols x params)
    size_t param_rows = calib.DLT_params.size();
    writePOD(ofs, param_rows);
    for (const auto& row : calib.DLT_params) {
        size_t param_cols = row.size();
        writePOD(ofs, param_cols);
        for (const auto& params : row) {
            writeVector(ofs, params);
        }
    }
}

void readCalibrationData(std::ifstream& ifs, CalibrationData& calib) {
    // Read DLT_paths (2D array of strings)
    size_t rows;
    readPOD(ifs, rows);
    calib.DLT_paths.resize(rows);
    for (auto& row : calib.DLT_paths) {
        size_t cols;
        readPOD(ifs, cols);
        row.resize(cols);
        for (auto& path : row) {
            readString(ifs, path);
        }
    }
    
    // Read DLT_params (3D array)
    size_t param_rows;
    readPOD(ifs, param_rows);
    calib.DLT_params.resize(param_rows);
    for (auto& row : calib.DLT_params) {
        size_t param_cols;
        readPOD(ifs, param_cols);
        row.resize(param_cols);
        for (auto& params : row) {
            readVector(ifs, params);
        }
    }
}

void writeDistortionData(std::ofstream& ofs, const DistortionData& dist) {
    // Write distortion_models (2D array of strings)
    size_t rows = dist.distortion_models.size();
    writePOD(ofs, rows);
    for (const auto& row : dist.distortion_models) {
        size_t cols = row.size();
        writePOD(ofs, cols);
        for (const auto& model : row) {
            writeString(ofs, model);
        }
    }
    
    // Write distortion_paths (2D array of strings)
    size_t path_rows = dist.distortion_paths.size();
    writePOD(ofs, path_rows);
    for (const auto& row : dist.distortion_paths) {
        size_t cols = row.size();
        writePOD(ofs, cols);
        for (const auto& path : row) {
            writeString(ofs, path);
        }
    }
}

void writeStringMap(std::ofstream& ofs, const std::map<std::string, std::string>& map) {
    size_t size = map.size();
    writePOD(ofs, size);
    for (const auto& [key, value] : map) {
        writeString(ofs, key);
        writeString(ofs, value);
    }
}

void readDistortionData(std::ifstream& ifs, DistortionData& dist) {
    // Read distortion_models (2D array of strings)
    size_t rows;
    readPOD(ifs, rows);
    dist.distortion_models.resize(rows);
    for (auto& row : dist.distortion_models) {
        size_t cols;
        readPOD(ifs, cols);
        row.resize(cols);
        for (auto& model : row) {
            readString(ifs, model);
        }
    }
    
    // Read distortion_paths (2D array of strings)
    size_t path_rows;
    readPOD(ifs, path_rows);
    dist.distortion_paths.resize(path_rows);
    for (auto& row : dist.distortion_paths) {
        size_t cols;
        readPOD(ifs, cols);
        row.resize(cols);
        for (auto& path : row) {
            readString(ifs, path);
        }
    }
}

void readStringMap(std::ifstream& ifs, std::map<std::string, std::string>& map) {
    size_t size;
    readPOD(ifs, size);
    map.clear();
    for (size_t i = 0; i < size; ++i) {
        std::string key, value;
        readString(ifs, key);
        readString(ifs, value);
        map[key] = value;
    }
}

// DIC3Dcombined binary serialization
void DIC3Dcombined::saveBinary(const std::string& filepath) const {
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filepath);
    }
    
    // Write magic number and version for format validation
    uint32_t magic = 0x44494333;  // "DIC3"
    uint32_t version = 1;
    writePOD(ofs, magic);
    writePOD(ofs, version);
    
    // Write main fields
    writeVector(ofs, pairIndices);
    
    // Write Points3D (vector of Points3D)
    size_t num_frames = Points3D.size();
    writePOD(ofs, num_frames);
    for (const auto& pts : Points3D) {
        writePoints3D(ofs, pts);
    }
    
    writeVector(ofs, Faces);
    writeVector(ofs, FaceColors);
    writeVector2D(ofs, corrComb);
    writeVector2D(ofs, FaceCorrComb);
    writeVector2D(ofs, FaceCentroids);
    writeDispData(ofs, Disp);
    writeCalibrationData(ofs, calibration);
    writeDistortionData(ofs, distortion);
    writeVector(ofs, FacePairInds);
    writeVector(ofs, PointPairInds);
    
    // Note: Not serializing DIC2Dinfo and AllPairsResults for simplicity
    // They can be added if needed
    uint8_t has_dic2dinfo = 0;
    uint8_t has_allpairs = 0;
    writePOD(ofs, has_dic2dinfo);
    writePOD(ofs, has_allpairs);
    
    if (!ofs.good()) {
        throw std::runtime_error("Error writing binary data to: " + filepath);
    }
    
    ofs.close();
    LOG_DEBUG << "DIC3Dcombined saved to binary: " << filepath;
}

DIC3Dcombined DIC3Dcombined::loadBinary(const std::string& filepath) {
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs.is_open()) {
        throw std::runtime_error("Failed to open file for reading: " + filepath);
    }
    
    // Validate magic number and version
    uint32_t magic, version;
    readPOD(ifs, magic);
    readPOD(ifs, version);
    if (magic != 0x44494333) {
        throw std::runtime_error("Invalid file format (magic number mismatch): " + filepath);
    }
    if (version != 1) {
        throw std::runtime_error("Unsupported file version: " + std::to_string(version));
    }
    
    DIC3Dcombined combined;
    
    // Read main fields
    readVector(ifs, combined.pairIndices);
    
    // Read Points3D (vector of Points3D)
    size_t num_frames;
    readPOD(ifs, num_frames);
    combined.Points3D.resize(num_frames);
    for (auto& pts : combined.Points3D) {
        readPoints3D(ifs, pts);
    }
    
    readVector(ifs, combined.Faces);
    readVector(ifs, combined.FaceColors);
    readVector2D(ifs, combined.corrComb);
    readVector2D(ifs, combined.FaceCorrComb);
    readVector2D(ifs, combined.FaceCentroids);
    readDispData(ifs, combined.Disp);
    readCalibrationData(ifs, combined.calibration);
    readDistortionData(ifs, combined.distortion);
    readVector(ifs, combined.FacePairInds);
    readVector(ifs, combined.PointPairInds);
    
    // Check for optional fields
    uint8_t has_dic2dinfo, has_allpairs;
    readPOD(ifs, has_dic2dinfo);
    readPOD(ifs, has_allpairs);
    
    if (!ifs.good()) {
        throw std::runtime_error("Error reading binary data from: " + filepath);
    }
    
    ifs.close();
    LOG_DEBUG << "DIC3Dcombined loaded from binary: " << filepath;
    LOG_DEBUG << "  - Frames: " << combined.Points3D.size()
              << ", Points: " << (combined.Points3D.empty() ? 0 : combined.Points3D[0].x.size())
              << ", Faces: " << (combined.Faces.size() / 3);

    return combined;
}

} // namespace cppxdic
