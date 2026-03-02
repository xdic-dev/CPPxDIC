/**
 * DataSerializer implementations for mat, bin, and json formats
 */

#include "data_serializer.h"
#include "mat_writer.h"
#include "mat_reader.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace cppxdic {

// ============================================================================
// DataSerializer base
// ============================================================================

bool DataSerializer::fileExists(const std::string& basePath) const {
    return std::filesystem::exists(basePath + extension());
}

std::unique_ptr<DataSerializer> DataSerializer::create(const std::string& format) {
    if (format == "mat") {
        return std::make_unique<MatSerializer>();
    } else if (format == "bin") {
        return std::make_unique<BinarySerializer>();
    } else if (format == "json") {
        return std::make_unique<JsonSerializer>();
    } else {
        throw std::invalid_argument("Unknown data format: '" + format +
            "'. Supported formats: mat, bin, json");
    }
}

// ============================================================================
// MatSerializer — delegates to MatWriter / MatReader
// ============================================================================

bool MatSerializer::saveDIC3Dcombined(const std::string& path, const DIC3Dcombined& data) {
    return MatWriter::write3DCombinedResults(path, data);
}

bool MatSerializer::loadDIC3Dcombined(const std::string& path, DIC3Dcombined& data) {
    return MatReader::readDIC3Dcombined(path, data);
}

bool MatSerializer::saveDIC3DPPresults(const std::string& path, const DIC3DPPresults& data) {
    return MatWriter::write3DPPresults(path, data);
}

bool MatSerializer::saveDIC2DPairResults(const std::string& path, const DIC2DPairResults& data) {
    return MatWriter::writeDIC2DPairResults(path, data);
}

bool MatSerializer::loadDIC2DPairResults(const std::string& path, DIC2DPairResults& data) {
    return MatReader::readDIC2DPairResults(path, data);
}

// ============================================================================
// BinarySerializer — delegates to saveBinary / loadBinary
// ============================================================================

bool BinarySerializer::saveDIC3Dcombined(const std::string& path, const DIC3Dcombined& data) {
    try {
        data.saveBinary(path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "BinarySerializer: Failed to save DIC3Dcombined: " << e.what() << std::endl;
        return false;
    }
}

bool BinarySerializer::loadDIC3Dcombined(const std::string& path, DIC3Dcombined& data) {
    try {
        data = DIC3Dcombined::loadBinary(path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "BinarySerializer: Failed to load DIC3Dcombined: " << e.what() << std::endl;
        return false;
    }
}

bool BinarySerializer::saveDIC3DPPresults(const std::string& path, const DIC3DPPresults& data) {
    try {
        data.saveBinary(path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "BinarySerializer: Failed to save DIC3DPPresults: " << e.what() << std::endl;
        return false;
    }
}

bool BinarySerializer::saveDIC2DPairResults(const std::string& path, const DIC2DPairResults& data) {
    // Binary format for DIC2DPairResults - write key fields
    try {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) return false;
        
        // Magic + version
        uint32_t magic = 0x44324450;  // "D2DP"
        uint32_t version = 1;
        ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));
        
        // Core fields
        ofs.write(reinterpret_cast<const char*>(&data.nCamRef), sizeof(data.nCamRef));
        ofs.write(reinterpret_cast<const char*>(&data.nCamDef), sizeof(data.nCamDef));
        ofs.write(reinterpret_cast<const char*>(&data.nImages), sizeof(data.nImages));
        
        // Faces
        size_t nf = data.Faces.size();
        ofs.write(reinterpret_cast<const char*>(&nf), sizeof(nf));
        if (nf > 0) ofs.write(reinterpret_cast<const char*>(data.Faces.data()), 
                              static_cast<std::streamsize>(nf * sizeof(int)));
        
        // FaceColors
        size_t nc = data.FaceColors.size();
        ofs.write(reinterpret_cast<const char*>(&nc), sizeof(nc));
        if (nc > 0) ofs.write(reinterpret_cast<const char*>(data.FaceColors.data()),
                              static_cast<std::streamsize>(nc * sizeof(double)));
        
        // Points (per frame)
        size_t np = data.Points.size();
        ofs.write(reinterpret_cast<const char*>(&np), sizeof(np));
        for (const auto& pts : data.Points) {
            size_t sz = pts.x.size();
            ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
            if (sz > 0) {
                ofs.write(reinterpret_cast<const char*>(pts.x.data()), static_cast<std::streamsize>(sz * sizeof(double)));
                ofs.write(reinterpret_cast<const char*>(pts.y.data()), static_cast<std::streamsize>(sz * sizeof(double)));
            }
        }
        
        // CorCoeffVec (per frame)
        size_t ncc = data.CorCoeffVec.size();
        ofs.write(reinterpret_cast<const char*>(&ncc), sizeof(ncc));
        for (const auto& cc : data.CorCoeffVec) {
            size_t sz = cc.size();
            ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
            if (sz > 0) ofs.write(reinterpret_cast<const char*>(cc.data()), static_cast<std::streamsize>(sz * sizeof(double)));
        }
        
        return ofs.good();
    } catch (const std::exception& e) {
        std::cerr << "BinarySerializer: Failed to save DIC2DPairResults: " << e.what() << std::endl;
        return false;
    }
}

bool BinarySerializer::loadDIC2DPairResults(const std::string& path, DIC2DPairResults& data) {
    try {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) return false;
        
        uint32_t magic, version;
        ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (magic != 0x44324450 || version != 1) return false;
        
        ifs.read(reinterpret_cast<char*>(&data.nCamRef), sizeof(data.nCamRef));
        ifs.read(reinterpret_cast<char*>(&data.nCamDef), sizeof(data.nCamDef));
        ifs.read(reinterpret_cast<char*>(&data.nImages), sizeof(data.nImages));
        
        size_t nf;
        ifs.read(reinterpret_cast<char*>(&nf), sizeof(nf));
        data.Faces.resize(nf);
        if (nf > 0) ifs.read(reinterpret_cast<char*>(data.Faces.data()), static_cast<std::streamsize>(nf * sizeof(int)));
        
        size_t nc;
        ifs.read(reinterpret_cast<char*>(&nc), sizeof(nc));
        data.FaceColors.resize(nc);
        if (nc > 0) ifs.read(reinterpret_cast<char*>(data.FaceColors.data()), static_cast<std::streamsize>(nc * sizeof(double)));
        
        size_t np;
        ifs.read(reinterpret_cast<char*>(&np), sizeof(np));
        data.Points.resize(np);
        for (auto& pts : data.Points) {
            size_t sz;
            ifs.read(reinterpret_cast<char*>(&sz), sizeof(sz));
            pts.x.resize(sz);
            pts.y.resize(sz);
            if (sz > 0) {
                ifs.read(reinterpret_cast<char*>(pts.x.data()), static_cast<std::streamsize>(sz * sizeof(double)));
                ifs.read(reinterpret_cast<char*>(pts.y.data()), static_cast<std::streamsize>(sz * sizeof(double)));
            }
        }
        
        size_t ncc;
        ifs.read(reinterpret_cast<char*>(&ncc), sizeof(ncc));
        data.CorCoeffVec.resize(ncc);
        for (auto& cc : data.CorCoeffVec) {
            size_t sz;
            ifs.read(reinterpret_cast<char*>(&sz), sizeof(sz));
            cc.resize(sz);
            if (sz > 0) ifs.read(reinterpret_cast<char*>(cc.data()), static_cast<std::streamsize>(sz * sizeof(double)));
        }
        
        return ifs.good();
    } catch (const std::exception& e) {
        std::cerr << "BinarySerializer: Failed to load DIC2DPairResults: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// JsonSerializer — uses nlohmann/json following MATLAB structure
// ============================================================================

// Helper: serialize Points3D to JSON array
static nlohmann::json points3dToJson(const Points3D& pts) {
    nlohmann::json j;
    j["x"] = pts.x;
    j["y"] = pts.y;
    j["z"] = pts.z;
    return j;
}

// Helper: deserialize Points3D from JSON
static Points3D points3dFromJson(const nlohmann::json& j) {
    Points3D pts;
    pts.x = j["x"].get<std::vector<double>>();
    pts.y = j["y"].get<std::vector<double>>();
    pts.z = j["z"].get<std::vector<double>>();
    return pts;
}

bool JsonSerializer::saveDIC3Dcombined(const std::string& path, const DIC3Dcombined& data) {
    try {
        nlohmann::json j;
        j["format"] = "DIC3Dcombined";
        j["version"] = 1;
        
        j["pairIndices"] = data.pairIndices;
        j["Faces"] = data.Faces;
        j["FaceColors"] = data.FaceColors;
        j["FacePairInds"] = data.FacePairInds;
        j["PointPairInds"] = data.PointPairInds;
        
        // Points3D per frame
        nlohmann::json pts_array = nlohmann::json::array();
        for (const auto& pts : data.Points3D) {
            pts_array.push_back(points3dToJson(pts));
        }
        j["Points3D"] = pts_array;
        
        // Correlation
        j["corrComb"] = data.corrComb;
        j["FaceCorrComb"] = data.FaceCorrComb;
        j["FaceCentroids"] = data.FaceCentroids;
        
        // Displacement
        j["Disp"]["DispVec"] = data.Disp.DispVec;
        j["Disp"]["DispMgn"] = data.Disp.DispMgn;
        
        // Calibration
        j["calibration"]["DLT_paths"] = data.calibration.DLT_paths;
        j["calibration"]["DLT_params"] = data.calibration.DLT_params;
        
        // Distortion
        j["distortion"]["distortion_models"] = data.distortion.distortion_models;
        j["distortion"]["distortion_paths"] = data.distortion.distortion_paths;
        
        std::ofstream ofs(path);
        if (!ofs.is_open()) return false;
        ofs << j.dump(2) << std::endl;
        return ofs.good();
    } catch (const std::exception& e) {
        std::cerr << "JsonSerializer: Failed to save DIC3Dcombined: " << e.what() << std::endl;
        return false;
    }
}

bool JsonSerializer::loadDIC3Dcombined(const std::string& path, DIC3Dcombined& data) {
    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;
        
        nlohmann::json j = nlohmann::json::parse(ifs);
        
        data.pairIndices = j.value("pairIndices", std::vector<int>{});
        data.Faces = j.value("Faces", std::vector<int>{});
        data.FaceColors = j.value("FaceColors", std::vector<double>{});
        data.FacePairInds = j.value("FacePairInds", std::vector<int>{});
        data.PointPairInds = j.value("PointPairInds", std::vector<int>{});
        
        // Points3D
        data.Points3D.clear();
        if (j.contains("Points3D")) {
            for (const auto& pt_j : j["Points3D"]) {
                data.Points3D.push_back(points3dFromJson(pt_j));
            }
        }
        
        // Correlation
        data.corrComb = j.value("corrComb", std::vector<std::vector<double>>{});
        data.FaceCorrComb = j.value("FaceCorrComb", std::vector<std::vector<double>>{});
        data.FaceCentroids = j.value("FaceCentroids", std::vector<std::vector<double>>{});
        
        // Displacement
        if (j.contains("Disp")) {
            data.Disp.DispVec = j["Disp"].value("DispVec", std::vector<std::vector<double>>{});
            data.Disp.DispMgn = j["Disp"].value("DispMgn", std::vector<std::vector<double>>{});
        }
        
        // Calibration
        if (j.contains("calibration")) {
            data.calibration.DLT_paths = j["calibration"].value("DLT_paths", std::vector<std::vector<std::string>>{});
            data.calibration.DLT_params = j["calibration"].value("DLT_params", std::vector<std::vector<std::vector<double>>>{});
        }
        
        // Distortion
        if (j.contains("distortion")) {
            data.distortion.distortion_models = j["distortion"].value("distortion_models", std::vector<std::vector<std::string>>{});
            data.distortion.distortion_paths = j["distortion"].value("distortion_paths", std::vector<std::vector<std::string>>{});
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "JsonSerializer: Failed to load DIC3Dcombined: " << e.what() << std::endl;
        return false;
    }
}

// Helper: serialize DeformGradient to JSON
static nlohmann::json deformGradientToJson(const DeformGradient& F) {
    nlohmann::json j;
    j["F11"] = F.F11; j["F12"] = F.F12; j["F13"] = F.F13;
    j["F21"] = F.F21; j["F22"] = F.F22; j["F23"] = F.F23;
    j["F31"] = F.F31; j["F32"] = F.F32; j["F33"] = F.F33;
    return j;
}

// Helper: serialize StrainTensor to JSON
static nlohmann::json strainTensorToJson(const StrainTensor& E) {
    nlohmann::json j;
    j["E11"] = E.E11; j["E12"] = E.E12; j["E13"] = E.E13;
    j["E21"] = E.E21; j["E22"] = E.E22; j["E23"] = E.E23;
    j["E31"] = E.E31; j["E32"] = E.E32; j["E33"] = E.E33;
    return j;
}

// Helper: serialize DeformData to JSON
static nlohmann::json deformDataToJson(const DeformData& deform) {
    nlohmann::json j;
    nlohmann::json f_arr = nlohmann::json::array();
    for (const auto& f : deform.F) f_arr.push_back(deformGradientToJson(f));
    j["F"] = f_arr;
    
    nlohmann::json s_arr = nlohmann::json::array();
    for (const auto& s : deform.strain) s_arr.push_back(strainTensorToJson(s));
    j["strain"] = s_arr;
    
    j["princStrain"] = deform.princStrain;
    j["maxShearStrain"] = deform.maxShearStrain;
    return j;
}

bool JsonSerializer::saveDIC3DPPresults(const std::string& path, const DIC3DPPresults& data) {
    try {
        nlohmann::json j;
        j["format"] = "DIC3DPPresults";
        j["version"] = 1;
        j["deftype"] = data.deftype;
        j["n_frames"] = data.n_frames;
        
        // Inherited DIC3Dcombined fields
        j["pairIndices"] = data.pairIndices;
        j["Faces"] = data.Faces;
        j["FaceColors"] = data.FaceColors;
        j["FacePairInds"] = data.FacePairInds;
        j["PointPairInds"] = data.PointPairInds;
        
        nlohmann::json pts_array = nlohmann::json::array();
        for (const auto& pts : data.Points3D) {
            pts_array.push_back(points3dToJson(pts));
        }
        j["Points3D"] = pts_array;
        
        j["corrComb"] = data.corrComb;
        j["FaceCorrComb"] = data.FaceCorrComb;
        j["FaceCentroids"] = data.FaceCentroids;
        j["FaceIsoInd"] = data.FaceIsoInd;
        
        j["Disp"]["DispVec"] = data.Disp.DispVec;
        j["Disp"]["DispMgn"] = data.Disp.DispMgn;
        
        // Deformation data
        j["Deform"] = deformDataToJson(data.Deform);
        j["Deform_ARBM"] = deformDataToJson(data.Deform_ARBM);
        
        // RBM
        j["RBM"]["RotMat"] = data.RBM.RotMat;
        j["RBM"]["TransVec"] = data.RBM.TransVec;
        
        // Points3D ARBM
        j["Points3D_ARBM_x"] = data.Points3D_ARBM_x;
        j["Points3D_ARBM_y"] = data.Points3D_ARBM_y;
        j["Points3D_ARBM_z"] = data.Points3D_ARBM_z;
        
        // Calibration & distortion
        j["calibration"]["DLT_paths"] = data.calibration.DLT_paths;
        j["calibration"]["DLT_params"] = data.calibration.DLT_params;
        j["distortion"]["distortion_models"] = data.distortion.distortion_models;
        j["distortion"]["distortion_paths"] = data.distortion.distortion_paths;
        
        std::ofstream ofs(path);
        if (!ofs.is_open()) return false;
        ofs << j.dump(2) << std::endl;
        return ofs.good();
    } catch (const std::exception& e) {
        std::cerr << "JsonSerializer: Failed to save DIC3DPPresults: " << e.what() << std::endl;
        return false;
    }
}

bool JsonSerializer::saveDIC2DPairResults(const std::string& path, const DIC2DPairResults& data) {
    try {
        nlohmann::json j;
        j["format"] = "DIC2DPairResults";
        j["version"] = 1;
        j["nCamRef"] = data.nCamRef;
        j["nCamDef"] = data.nCamDef;
        j["nImages"] = data.nImages;
        j["Faces"] = data.Faces;
        j["FaceColors"] = data.FaceColors;
        
        nlohmann::json pts_array = nlohmann::json::array();
        for (const auto& pts : data.Points) {
            nlohmann::json pj;
            pj["x"] = pts.x;
            pj["y"] = pts.y;
            pts_array.push_back(pj);
        }
        j["Points"] = pts_array;
        
        j["CorCoeffVec"] = data.CorCoeffVec;
        
        std::ofstream ofs(path);
        if (!ofs.is_open()) return false;
        ofs << j.dump(2) << std::endl;
        return ofs.good();
    } catch (const std::exception& e) {
        std::cerr << "JsonSerializer: Failed to save DIC2DPairResults: " << e.what() << std::endl;
        return false;
    }
}

bool JsonSerializer::loadDIC2DPairResults(const std::string& path, DIC2DPairResults& data) {
    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;
        
        nlohmann::json j = nlohmann::json::parse(ifs);
        
        data.nCamRef = j.value("nCamRef", 0);
        data.nCamDef = j.value("nCamDef", 0);
        data.nImages = j.value("nImages", 0);
        data.Faces = j.value("Faces", std::vector<int>{});
        data.FaceColors = j.value("FaceColors", std::vector<double>{});
        
        data.Points.clear();
        if (j.contains("Points")) {
            for (const auto& pj : j["Points"]) {
                Points2D pts;
                pts.x = pj.value("x", std::vector<double>{});
                pts.y = pj.value("y", std::vector<double>{});
                data.Points.push_back(pts);
            }
        }
        
        data.CorCoeffVec = j.value("CorCoeffVec", std::vector<std::vector<double>>{});
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "JsonSerializer: Failed to load DIC2DPairResults: " << e.what() << std::endl;
        return false;
    }
}

} // namespace cppxdic
