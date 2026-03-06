/**
 * MAT File Reader Implementation
 */

#include "mat_reader.h"
#include "dic_structures.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cstdint>

namespace cppxdic {

// ============================================================================
// RAII convenience helpers
// ============================================================================

MatReader::MatFilePtr MatReader::openMat(const std::string& path) {
    return MatFilePtr(Mat_Open(path.c_str(), MAT_ACC_RDONLY));
}

MatReader::MatVarPtr MatReader::readVar(mat_t* matfp, const std::string& name) {
    if (!matfp) return MatVarPtr{};
    return MatVarPtr(Mat_VarRead(matfp, name.c_str()));
}

MatReader::MatVarPtr MatReader::readFirstVar(mat_t* matfp) {
    if (!matfp) return MatVarPtr{};
    return MatVarPtr(Mat_VarRead(matfp, nullptr));
}

// ============================================================================
// Internal helpers
// ============================================================================

matvar_t* MatReader::getStructField(matvar_t* s, const char* field, size_t index) noexcept {
    if (!s || s->class_type != MAT_C_STRUCT) return nullptr;
    return Mat_VarGetStructFieldByName(s, field, index);
}

bool MatReader::readTopInt(mat_t* matfp, const char* name, int& out) noexcept {
    out = 0;
    MatVarPtr v(Mat_VarRead(matfp, name));
    if (!v || !v->data) return false;
    out = static_cast<int>(readScalar(v.get()));
    return true;
}

bool MatReader::readTopDouble(mat_t* matfp, const char* name, double& out) noexcept {
    out = 0.0;
    MatVarPtr v(Mat_VarRead(matfp, name));
    if (!v || !v->data) return false;
    out = readScalar(v.get());
    return true;
}

bool MatReader::readTopString(mat_t* matfp, const char* name, std::string& out) {
    out.clear();
    MatVarPtr v(Mat_VarRead(matfp, name));
    if (!v || !v->data) return false;
    out = readString(v.get());
    return true;
}

bool MatReader::readStructInt(matvar_t* s, const char* field, int& out, size_t index) noexcept {
    out = 0;
    matvar_t* f = getStructField(s, field, index);
    if (!f || !f->data) return false;
    out = static_cast<int>(readScalar(f));
    return true;
}

bool MatReader::readStructDouble(matvar_t* s, const char* field, double& out, size_t index) noexcept {
    out = 0.0;
    matvar_t* f = getStructField(s, field, index);
    if (!f || !f->data) return false;
    out = readScalar(f);
    return true;
}

bool MatReader::readStructBool(matvar_t* s, const char* field, bool& out, size_t index) noexcept {
    out = false;
    matvar_t* f = getStructField(s, field, index);
    if (!f || !f->data) return false;
    out = static_cast<int>(readScalar(f)) != 0;
    return true;
}

bool MatReader::readStructString(matvar_t* s, const char* field, std::string& out, size_t index) {
    out.clear();
    matvar_t* f = getStructField(s, field, index);
    if (!f || !f->data) return false;
    out = readString(f);
    return true;
}

// ============================================================================
// DLT Calibration Loading
// ============================================================================

bool MatReader::loadDLTCalibration(const std::string& mat_path, DLTCalibrationData& calib) {
    auto matfp = openMat(mat_path);
    if (!matfp) {
        std::cerr << "Failed to open DLT calibration file: " << mat_path << '\n';
        return false;
    }

    auto dlt_var = readVar(matfp.get(), "DLTstructCam");
    if (!dlt_var || dlt_var->class_type != MAT_C_STRUCT) {
        std::cerr << "Variable 'DLTstructCam' not found or not a struct in " << mat_path << '\n';
        return false;
    }

    calib.filePath = mat_path;

    // DLTparams (float64 1d array, 11 elements)
    if (matvar_t* f = getStructField(dlt_var.get(), "DLTparams", 0)) {
        calib.DLTparams = readDoubleArray(f);
    }
    if (calib.DLTparams.size() != 11) {
        std::cerr << "DLTparams has " << calib.DLTparams.size() << " elements (expected 11) in " << mat_path << '\n';
        return false;
    }

    // indCam (int scalar)
    readStructInt(dlt_var.get(), "indCam", calib.indCam);

    // imageCentroids (float64 Nx2, column-major in MATLAB)
    if (matvar_t* f = getStructField(dlt_var.get(), "imageCentroids", 0)) {
        if (f->data && f->class_type == MAT_C_DOUBLE && f->rank >= 2) {
            size_t rows = f->dims[0];
            size_t cols = f->dims[1];
            calib.imageCentroids_rows = rows;
            const double* d = static_cast<const double*>(f->data);
            // Convert column-major to row-major (Nx2)
            calib.imageCentroids.resize(rows * cols);
            for (size_t r = 0; r < rows; ++r) {
                for (size_t c = 0; c < cols; ++c) {
                    calib.imageCentroids[r * cols + c] = d[c * rows + r];
                }
            }
        }
    }

    // columns (uint8 1d array)
    if (matvar_t* f = getStructField(dlt_var.get(), "columns", 0)) {
        if (f->data) {
            size_t n = 1;
            for (int i = 0; i < f->rank; ++i) n *= f->dims[i];
            if (f->class_type == MAT_C_UINT8) {
                const uint8_t* d = static_cast<const uint8_t*>(f->data);
                calib.columns.assign(d, d + n);
            } else if (f->class_type == MAT_C_DOUBLE) {
                const double* d = static_cast<const double*>(f->data);
                calib.columns.resize(n);
                for (size_t i = 0; i < n; ++i) calib.columns[i] = static_cast<uint8_t>(d[i]);
            }
        }
    }

    // C3Dtrue (float64 3d array dim0 x dim1 x 3, column-major)
    if (matvar_t* f = getStructField(dlt_var.get(), "C3Dtrue", 0)) {
        if (f->data && f->class_type == MAT_C_DOUBLE && f->rank >= 3) {
            size_t d0 = f->dims[0];
            size_t d1 = f->dims[1];
            size_t d2 = f->dims[2];  // should be 3
            calib.C3Dtrue_dim0 = d0;
            calib.C3Dtrue_dim1 = d1;
            const double* src = static_cast<const double*>(f->data);
            // Convert column-major (d0 x d1 x d2) to row-major
            calib.C3Dtrue.resize(d0 * d1 * d2);
            for (size_t k = 0; k < d2; ++k) {
                for (size_t j = 0; j < d1; ++j) {
                    for (size_t i = 0; i < d0; ++i) {
                        calib.C3Dtrue[(i * d1 + j) * d2 + k] = src[k * d1 * d0 + j * d0 + i];
                    }
                }
            }
        }
    }

    std::cout << "Loaded DLT calibration: cam " << calib.indCam
              << ", " << calib.DLTparams.size() << " DLT params"
              << ", " << calib.imageCentroids_rows << " centroids"
              << " from " << mat_path << '\n';

    return true;
}

// ============================================================================
// Protocol Loading
// ============================================================================

bool MatReader::loadProtocol(const std::string& protocol_path, ProtocolFileData& protocol) {
    auto matfp = openMat(protocol_path);
    if (!matfp) {
        std::cerr << "Failed to open protocol file: " << protocol_path << '\n';
        return false;
    }

    // Read 'cond' structure (or fallback to 'protocol')
    MatVarPtr cond_var(Mat_VarRead(matfp.get(), "cond"));
    if (!cond_var) {
        cond_var.reset(Mat_VarRead(matfp.get(), "protocol"));
    }

    if (!cond_var || cond_var->class_type != MAT_C_STRUCT) {
        return false;
    }

    // Read titles field (cell array of strings)
    if (matvar_t* titles_var = getStructField(cond_var.get(), "titles", 0)) {
        if (titles_var->class_type == MAT_C_CELL) {
            protocol.titles = readCellStrings(titles_var);
        }
    }

    // Read table field
    matvar_t* table_var = getStructField(cond_var.get(), "table", 0);
    if (!table_var || table_var->class_type != MAT_C_CELL || table_var->rank < 2) {
        return false;
    }

    const size_t n_trials = table_var->dims[0];
    const size_t n_fields = table_var->dims[1];

    protocol.n_trials = n_trials;
    protocol.n_fields = n_fields;
    protocol.trials.clear();
    protocol.trials.reserve(n_trials);

    // Find column indices for important fields
    int dir_idx = -1, nf_idx = -1, spd_idx = -1, rep_idx = -1;
    for (size_t i = 0; i < protocol.titles.size(); ++i) {
        if (protocol.titles[i] == "dir") dir_idx = static_cast<int>(i);
        else if (protocol.titles[i] == "nf") nf_idx = static_cast<int>(i);
        else if (protocol.titles[i] == "spd") spd_idx = static_cast<int>(i);
        else if (protocol.titles[i] == "rep") rep_idx = static_cast<int>(i);
    }

    // Read trial data
    for (size_t trial = 0; trial < n_trials; ++trial) {
        ProtocolFileData::TrialEntry info{};
        info.trial_number = static_cast<int>(trial + 1);

        auto readCellAt = [&](size_t col) -> matvar_t* {
            // Mat_VarGetCell uses linear index = row + col*rows for MATLAB-style 2D cells.
            // Your prior code used trial*n_fields + col (row-major). That *can* be wrong depending on matio layout.
            // To be safe, use matio’s Mat_VarGetCell with MATLAB linear indexing:
            // idx = trial + col*n_trials
            const size_t idx = trial + col * n_trials;
            return Mat_VarGetCell(table_var, idx);
        };

        // direction
        if (dir_idx >= 0) {
            matvar_t* cell = readCellAt(static_cast<size_t>(dir_idx));
            if (cell && cell->class_type == MAT_C_CHAR && cell->data) {
                std::string s(static_cast<char*>(cell->data), cell->nbytes);
                s.erase(s.find_last_not_of(" \t\n\r") + 1);
                info.direction = std::move(s);
            }
        }

        // force
        if (nf_idx >= 0) {
            matvar_t* cell = readCellAt(static_cast<size_t>(nf_idx));
            if (cell && cell->class_type == MAT_C_DOUBLE && cell->data) {
                info.force = *static_cast<double*>(cell->data);
            }
        }

        // speed
        if (spd_idx >= 0) {
            matvar_t* cell = readCellAt(static_cast<size_t>(spd_idx));
            if (cell && cell->class_type == MAT_C_DOUBLE && cell->data) {
                info.speed = *static_cast<double*>(cell->data);
            }
        }

        // repetition
        if (rep_idx >= 0) {
            matvar_t* cell = readCellAt(static_cast<size_t>(rep_idx));
            if (cell && cell->class_type == MAT_C_DOUBLE && cell->data) {
                info.repetition = static_cast<int>(*static_cast<double*>(cell->data));
            }
        }

        protocol.trials.push_back(std::move(info));
    }

    return true; // matfp auto-closed
}

std::vector<std::string> MatReader::readCellStrings(matvar_t* cell_var) {
    std::vector<std::string> strings;
    if (!cell_var || cell_var->class_type != MAT_C_CELL) return strings;

    const size_t n_cells = getCellArraySize(cell_var);
    strings.reserve(n_cells);

    for (size_t i = 0; i < n_cells; ++i) {
        matvar_t* cell = Mat_VarGetCell(cell_var, i);
        if (cell && cell->class_type == MAT_C_CHAR && cell->data) {
            std::string str(static_cast<char*>(cell->data), cell->nbytes);
            str.erase(str.find_last_not_of(" \t\n\r") + 1);
            strings.push_back(std::move(str));
        } else {
            strings.emplace_back();
        }
    }
    return strings;
}

// ============================================================================
// Struct field readers (legacy raw-pointer API preserved)
// ============================================================================

matvar_t* MatReader::readStructField(mat_t* matfp,
                                    const std::string& struct_name,
                                    const std::string& field_name) {
    if (!matfp) return nullptr;

    MatVarPtr struct_var(Mat_VarRead(matfp, struct_name.c_str()));
    if (!struct_var || struct_var->class_type != MAT_C_STRUCT) return nullptr;

    matvar_t* field = Mat_VarGetStructFieldByName(struct_var.get(), field_name.c_str(), 0);
    if (!field) return nullptr;

    // Deep copy, caller frees
    return Mat_VarDuplicate(field, 1);
}

matvar_t* MatReader::readNestedField(mat_t* matfp, const std::string& path) {
    if (!matfp) return nullptr;

    // Split path by '.'
    std::vector<std::string> parts;
    std::istringstream iss(path);
    std::string part;
    while (std::getline(iss, part, '.')) parts.push_back(part);
    if (parts.empty()) return nullptr;

    MatVarPtr current(Mat_VarRead(matfp, parts[0].c_str()));
    if (!current) return nullptr;

    for (size_t i = 1; i < parts.size(); ++i) {
        if (current->class_type != MAT_C_STRUCT) return nullptr;

        matvar_t* field = Mat_VarGetStructFieldByName(current.get(), parts[i].c_str(), 0);
        if (!field) return nullptr;

        MatVarPtr next(Mat_VarDuplicate(field, 1)); // deep copy
        current.swap(next);
    }

    return current.release(); // legacy API
}

// ============================================================================
// Primitive readers (kept mostly as-is, small safety tweaks)
// ============================================================================

std::vector<double> MatReader::readDoubleArray(matvar_t* var) {
    std::vector<double> result;
    if (!var || !var->data) return result;

    if (var->class_type != MAT_C_DOUBLE) {
        std::cerr << "Warning: Variable is not double type\n";
        return result;
    }

    size_t n_elements = 1;
    for (int i = 0; i < var->rank; ++i) n_elements *= var->dims[i];

    const double* data = static_cast<const double*>(var->data);
    result.assign(data, data + n_elements);
    return result;
}

std::vector<double> MatReader::readDouble2DArray(matvar_t* var, size_t& rows, size_t& cols) {
    std::vector<double> result;
    rows = 0;
    cols = 0;

    if (!var || !var->data || var->rank < 2) return result;

    if (var->class_type != MAT_C_DOUBLE) {
        std::cerr << "Warning: Variable is not double type\n";
        return result;
    }

    rows = var->dims[0];
    cols = var->dims[1];

    const double* data = static_cast<const double*>(var->data);
    const size_t n_elements = rows * cols;

    // MATLAB column-major -> row-major
    result.resize(n_elements);
    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            result[r * cols + c] = data[c * rows + r];
        }
    }
    return result;
}

cv::Mat MatReader::readImage(matvar_t* var) {
    if (!var || !var->data) return cv::Mat();

    if (var->rank != 2) {
        std::cerr << "Warning: Expected 2D array for image\n";
        return cv::Mat();
    }

    const size_t rows = var->dims[0];
    const size_t cols = var->dims[1];

    if (var->class_type == MAT_C_DOUBLE) {
        const double* data = static_cast<const double*>(var->data);
        cv::Mat img(rows, cols, CV_64F);
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                img.at<double>(r, c) = data[c * rows + r];
            }
        }
        return img;
    }

    if (var->class_type == MAT_C_UINT8) {
        const uint8_t* data = static_cast<const uint8_t*>(var->data);
        cv::Mat img(rows, cols, CV_8U);

        const bool is_logical = (var->isLogical != 0);
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                const uint8_t value = data[c * rows + r];
                img.at<uint8_t>(r, c) = is_logical ? (value * 255) : value;
            }
        }
        return img;
    }

    std::cerr << "Warning: Unsupported image data type\n";
    return cv::Mat();
}

double MatReader::readScalar(matvar_t* var) {
    if (!var || !var->data) return 0.0;

    if (var->class_type == MAT_C_DOUBLE) {
        return static_cast<const double*>(var->data)[0];
    }
    if (var->class_type == MAT_C_INT32) {
        return static_cast<double>(static_cast<const int32_t*>(var->data)[0]);
    }
    if (var->class_type == MAT_C_UINT8) {
        return static_cast<double>(static_cast<const uint8_t*>(var->data)[0]);
    }

    std::cerr << "Warning: Unsupported scalar type\n";
    return 0.0;
}

std::string MatReader::readString(matvar_t* var) {
    if (!var || !var->data) return "";

    size_t len = 1;
    for (int i = 0; i < var->rank; ++i) len *= var->dims[i];

    if (var->class_type == MAT_C_CHAR) {
        return std::string(static_cast<const char*>(var->data), len);
    }

    if (var->class_type == MAT_C_UINT16) {
        const uint16_t* data = static_cast<const uint16_t*>(var->data);
        std::string out;
        out.reserve(len);
        for (size_t i = 0; i < len; ++i) out.push_back(static_cast<char>(data[i]));
        return out;
    }

    std::cerr << "Warning: Unsupported string type\n";
    return "";
}

size_t MatReader::getCellArraySize(matvar_t* var) {
    if (!var || var->class_type != MAT_C_CELL) return 0;

    size_t n_cells = 1;
    for (int i = 0; i < var->rank; ++i) n_cells *= var->dims[i];
    return n_cells;
}

matvar_t* MatReader::getCellElement(matvar_t* var, size_t index) {
    if (!var || var->class_type != MAT_C_CELL) return nullptr;
    const size_t n_cells = getCellArraySize(var);
    if (index >= n_cells) return nullptr;

    // Safer than peeking into var->data:
    return Mat_VarGetCell(var, index);
}

// ============================================================================
// Higher-level readers
// ============================================================================

std::map<std::string, double> MatReader::readDispInfo(mat_t* matfp) {
    std::map<std::string, double> params;
    if (!matfp) return params;

    MatVarPtr dispinfo(readNestedField(matfp, "data_dic_save.dispinfo"));
    if (!dispinfo || dispinfo->class_type != MAT_C_STRUCT) return params;

    const std::vector<std::string> field_names = {
        "cutoff_corrcoef", "cutoff_diffnorm", "cutoff_iteration",
        "radius", "spacing", "subsettrunc", "total_threads"
    };

    for (const auto& field : field_names) {
        matvar_t* field_var = Mat_VarGetStructFieldByName(dispinfo.get(), field.c_str(), 0);
        if (field_var && field_var->data) {
            params[field] = readScalar(field_var);
        }
    }

    return params;
}

bool MatReader::variableExists(mat_t* matfp, const std::string& var_name) {
    if (!matfp) return false;
    MatVarPtr var(Mat_VarRead(matfp, var_name.c_str()));
    return static_cast<bool>(var);
}

cv::Mat MatReader::readROIMask(const std::string& mat_path, const std::string& var_name) {
    auto matfp = openMat(mat_path);
    if (!matfp) {
        std::cerr << "Failed to open MAT file: " << mat_path << '\n';
        return cv::Mat();
    }

    auto var = readVar(matfp.get(), var_name);
    if (!var) {
        std::cerr << "Variable '" << var_name << "' not found in " << mat_path << '\n';
        return cv::Mat();
    }

    return readImage(var.get());
}

bool MatReader::readDIC3Dcombined(const std::string& mat_path, DIC3Dcombined& combined) {
    auto matfp = openMat(mat_path);
    if (!matfp) {
        std::cerr << "Failed to open MAT file: " << mat_path << '\n';
        return false;
    }

    // Read first variable, else try common names
    MatVarPtr dic3d_var = readFirstVar(matfp.get());
    if (!dic3d_var) {
        const char* var_names[] = {"DIC3Dcombined", "DIC3D", "data"};
        for (const char* name : var_names) {
            dic3d_var.reset(Mat_VarRead(matfp.get(), name));
            if (dic3d_var) break;
        }
    }

    if (!dic3d_var || dic3d_var->class_type != MAT_C_STRUCT) {
        std::cerr << "DIC3Dcombined structure not found in " << mat_path << '\n';
        return false;
    }

    // Faces (Nx3)
    if (matvar_t* faces_var = getStructField(dic3d_var.get(), "Faces", 0)) {
        if (faces_var->class_type == MAT_C_DOUBLE && faces_var->data && faces_var->rank >= 2) {
            const size_t nFaces = faces_var->dims[0];
            const double* faces_data = static_cast<const double*>(faces_var->data);

            combined.Faces.clear();
            combined.Faces.reserve(nFaces * 3);
            for (size_t i = 0; i < nFaces * 3; ++i) {
                combined.Faces.push_back(static_cast<int>(faces_data[i]) - 1);
            }
        }
    }

    // Points3D (cell frames)
    if (matvar_t* points3d_var = getStructField(dic3d_var.get(), "Points3D", 0)) {
        if (points3d_var->class_type == MAT_C_CELL) {
            const size_t nFrames = getCellArraySize(points3d_var);
            combined.Points3D.resize(nFrames);

            for (size_t frame = 0; frame < nFrames; ++frame) {
                matvar_t* frame_var = Mat_VarGetCell(points3d_var, frame);
                if (!frame_var || frame_var->class_type != MAT_C_DOUBLE || !frame_var->data || frame_var->rank < 2) continue;

                const size_t nPoints = frame_var->dims[0];
                const double* pts_data = static_cast<const double*>(frame_var->data);

                combined.Points3D[frame].x.resize(nPoints);
                combined.Points3D[frame].y.resize(nPoints);
                combined.Points3D[frame].z.resize(nPoints);

                for (size_t i = 0; i < nPoints; ++i) {
                    combined.Points3D[frame].x[i] = pts_data[i];
                    combined.Points3D[frame].y[i] = pts_data[i + nPoints];
                    combined.Points3D[frame].z[i] = pts_data[i + 2 * nPoints];
                }
            }
        }
    }

    // FacePairInds / PointPairInds
    if (matvar_t* facepair_var = getStructField(dic3d_var.get(), "FacePairInds", 0)) {
        if (facepair_var->class_type == MAT_C_DOUBLE && facepair_var->data) {
            const size_t n = facepair_var->dims[0];
            const double* data = static_cast<const double*>(facepair_var->data);
            combined.FacePairInds.assign(data, data + n);
        }
    }

    if (matvar_t* pointpair_var = getStructField(dic3d_var.get(), "PointPairInds", 0)) {
        if (pointpair_var->class_type == MAT_C_DOUBLE && pointpair_var->data) {
            const size_t n = pointpair_var->dims[0];
            const double* data = static_cast<const double*>(pointpair_var->data);
            combined.PointPairInds.assign(data, data + n);
        }
    }

    std::cout << "Loaded DIC3Dcombined: "
              << combined.Points3D.size() << " frames, "
              << (combined.Points3D.empty() ? 0 : combined.Points3D[0].x.size()) << " points, "
              << combined.Faces.size() / 3 << " faces\n";

    return true;
}

std::vector<int> MatReader::readIntArray(matvar_t* var) {
    std::vector<int> result;
    if (!var || !var->data) return result;

    size_t n_elements = 1;
    for (int i = 0; i < var->rank; ++i) n_elements *= var->dims[i];

    result.reserve(n_elements);

    if (var->class_type == MAT_C_DOUBLE) {
        const double* data = static_cast<const double*>(var->data);
        for (size_t i = 0; i < n_elements; ++i) result.push_back(static_cast<int>(data[i]));
        return result;
    }

    if (var->class_type == MAT_C_INT32) {
        const int32_t* data = static_cast<const int32_t*>(var->data);
        result.assign(data, data + n_elements);
        return result;
    }

    if (var->class_type == MAT_C_UINT16) {
        const uint16_t* data = static_cast<const uint16_t*>(var->data);
        for (size_t i = 0; i < n_elements; ++i) result.push_back(static_cast<int>(data[i]));
        return result;
    }

    if (var->class_type == MAT_C_UINT32) {
        const uint32_t* data = static_cast<const uint32_t*>(var->data);
        for (size_t i = 0; i < n_elements; ++i) result.push_back(static_cast<int>(data[i]));
        return result;
    }

    return result;
}

// ============================================================================
// Updated DIC2DPairResults (top-level fields, RAII, no leaks)
// ============================================================================

bool MatReader::readDIC2DPairResults(const std::string& mat_path, DIC2DPairResults& result) {
    auto matfp = openMat(mat_path);
    if (!matfp) {
        std::cerr << "Failed to open MAT file: " << mat_path << '\n';
        return false;
    }

    // Top-level scalars
    readTopInt(matfp.get(), "nCamRef", result.nCamRef);
    readTopInt(matfp.get(), "nCamDef", result.nCamDef);
    readTopInt(matfp.get(), "nImages", result.nImages);

    // ROImask
    if (auto roi_var = readVar(matfp.get(), "ROImask")) {
        result.ROImask = readImage(roi_var.get());
    }

    // Faces (Nx3 or 3xN)
    if (auto faces_var = readVar(matfp.get(), "Faces")) {
        if (faces_var->data && faces_var->rank >= 2) {
            const size_t rows = faces_var->dims[0];
            const size_t cols = faces_var->dims[1];
            const size_t nFaces = (cols == 3) ? rows : ((rows == 3) ? cols : 0);

            result.Faces.clear();

            if (nFaces == 0) {
                std::cerr << "Faces has unexpected shape (" << rows << "x" << cols
                          << "), expected Nx3 or 3xN in " << mat_path << '\n';
            } else {
                result.Faces.reserve(nFaces * 3);

                auto pushFaces = [&](auto* dataPtr) {
                    for (size_t i = 0; i < nFaces * 3; ++i) {
                        result.Faces.push_back(static_cast<int>(dataPtr[i]) - 1);
                    }
                };

                if (faces_var->class_type == MAT_C_DOUBLE) {
                    pushFaces(static_cast<const double*>(faces_var->data));
                } else if (faces_var->class_type == MAT_C_UINT16) {
                    pushFaces(static_cast<const uint16_t*>(faces_var->data));
                } else if (faces_var->class_type == MAT_C_UINT32) {
                    pushFaces(static_cast<const uint32_t*>(faces_var->data));
                } else if (faces_var->class_type == MAT_C_INT32) {
                    pushFaces(static_cast<const int32_t*>(faces_var->data));
                } else {
                    std::cerr << "Faces has unsupported type (class_type=" << faces_var->class_type
                              << ") in " << mat_path << '\n';
                }
            }
        }
    }

    // FaceColors
    if (auto facecolors_var = readVar(matfp.get(), "FaceColors")) {
        result.FaceColors = readDoubleArray(facecolors_var.get());
    }

    // Points (cell array)
    if (auto points_var = readVar(matfp.get(), "Points")) {
        if (points_var->class_type == MAT_C_CELL) {
            const size_t nFrames = getCellArraySize(points_var.get());
            result.Points.resize(nFrames);

            for (size_t frame = 0; frame < nFrames; ++frame) {
                matvar_t* frame_var = Mat_VarGetCell(points_var.get(), frame);
                if (!frame_var || frame_var->class_type != MAT_C_DOUBLE || !frame_var->data || frame_var->rank < 2)
                    continue;

                const size_t nPoints = frame_var->dims[0];
                const size_t nCols = frame_var->dims[1];
                if (nCols < 2) continue;

                const double* pts_data = static_cast<const double*>(frame_var->data);

                result.Points[frame].x.resize(nPoints);
                result.Points[frame].y.resize(nPoints);

                for (size_t i = 0; i < nPoints; ++i) {
                    result.Points[frame].x[i] = pts_data[i];
                    result.Points[frame].y[i] = pts_data[i + nPoints];
                }
            }
        }
    }

    // CorCoeffVec (cell array)
    if (auto corr_var = readVar(matfp.get(), "CorCoeffVec")) {
        if (corr_var->class_type == MAT_C_CELL) {
            const size_t nFrames = getCellArraySize(corr_var.get());
            result.CorCoeffVec.resize(nFrames);

            for (size_t frame = 0; frame < nFrames; ++frame) {
                matvar_t* frame_var = Mat_VarGetCell(corr_var.get(), frame);
                if (frame_var && frame_var->class_type == MAT_C_DOUBLE && frame_var->data) {
                    result.CorCoeffVec[frame] = readDoubleArray(frame_var);
                }
            }
        }
    }

    // ncorrInfo (struct)
    if (auto ncorr_var = readVar(matfp.get(), "ncorrInfo")) {
        if (ncorr_var->class_type == MAT_C_STRUCT) {
            if (matvar_t* cutoff_cc = getStructField(ncorr_var.get(), "cutoff_corrcoef", 0)) {
                result.ncorrInfo.cutoff_corrcoef = readDoubleArray(cutoff_cc);
            }

            readStructDouble(ncorr_var.get(), "cutoff_diffnorm", result.ncorrInfo.cutoff_diffnorm);
            readStructInt(ncorr_var.get(), "cutoff_iteration", result.ncorrInfo.cutoff_iteration);
            readStructInt(ncorr_var.get(), "radius", result.ncorrInfo.radius);
            readStructInt(ncorr_var.get(), "spacing", result.ncorrInfo.spacing);
            readStructDouble(ncorr_var.get(), "pixtounits", result.ncorrInfo.pixtounits);
            readStructInt(ncorr_var.get(), "lenscoef", result.ncorrInfo.lenscoef);
            readStructBool(ncorr_var.get(), "subsettrunc", result.ncorrInfo.subsettrunc);
            readStructInt(ncorr_var.get(), "total_threads", result.ncorrInfo.total_threads);
            readStructString(ncorr_var.get(), "type", result.ncorrInfo.type);
            readStructString(ncorr_var.get(), "units", result.ncorrInfo.units);

            // stepanalysis nested struct
            matvar_t* step_var = getStructField(ncorr_var.get(), "stepanalysis", 0);
            if (step_var && step_var->class_type == MAT_C_STRUCT) {
                readStructBool(step_var, "enabled", result.ncorrInfo.stepanalysis.enabled);
                readStructString(step_var, "type", result.ncorrInfo.stepanalysis.type);
                readStructBool(step_var, "auto", result.ncorrInfo.stepanalysis.auto_update);
                readStructInt(step_var, "step", result.ncorrInfo.stepanalysis.step);
            }
        }
    }

    std::cout << "Loaded DIC2DPairResults: "
              << "nCamRef=" << result.nCamRef
              << ", nCamDef=" << result.nCamDef
              << ", nImages=" << result.nImages
              << ", " << result.Points.size() << " frames"
              << ", " << result.Faces.size() / 3 << " faces\n";

    return true;
}

} // namespace cppxdic