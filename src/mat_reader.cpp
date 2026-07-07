/**
 * MAT File Reader Implementation
 */

#include "mat_reader.h"
#include "dic_structures.h"
#include "logging.h"
#include <iostream>
#include <matio.h>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <functional>

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
        LOG_ERROR << "Failed to open DLT calibration file: " << mat_path;
        return false;
    }

    auto dlt_var = readVar(matfp.get(), "DLTstructCam");
    if (!dlt_var || dlt_var->class_type != MAT_C_STRUCT) {
        LOG_ERROR << "Variable 'DLTstructCam' not found or not a struct in " << mat_path;
        return false;
    }

    calib.filePath = mat_path;

    // DLTparams (float64 1d array, 11 elements)
    if (matvar_t* f = getStructField(dlt_var.get(), "DLTparams", 0)) {
        calib.DLTparams = readDoubleArray(f);
    }
    if (calib.DLTparams.size() != 11) {
        LOG_ERROR << "DLTparams has " << calib.DLTparams.size() << " elements (expected 11) in " << mat_path;
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

    LOG_INFO << "Loaded DLT calibration: cam " << calib.indCam
             << ", " << calib.DLTparams.size() << " DLT params"
             << ", " << calib.imageCentroids_rows << " centroids"
             << " from " << mat_path;

    return true;
}

// ============================================================================
// Protocol Loading
// ============================================================================

bool MatReader::loadProtocol(const std::string& protocol_path, ProtocolFileData& protocol) {
    auto matfp = openMat(protocol_path);
    if (!matfp) {
        LOG_ERROR << "Failed to open protocol file: " << protocol_path;
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
        LOG_WARN << "Variable is not double type";
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
        LOG_WARN << "Variable is not double type";
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
        LOG_WARN << "Expected 2D array for image";
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

    LOG_WARN << "Unsupported image data type";
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

    LOG_WARN << "Unsupported scalar type";
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

    LOG_WARN << "Unsupported string type";
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
        LOG_ERROR << "Failed to open MAT file: " << mat_path;
        return cv::Mat();
    }

    auto var = readVar(matfp.get(), var_name);
    if (!var) {
        LOG_ERROR << "Variable '" << var_name << "' not found in " << mat_path;
        return cv::Mat();
    }

    return readImage(var.get());
}

bool MatReader::readDIC3Dcombined(const std::string& mat_path, DIC3Dcombined& combined) {
    auto matfp = openMat(mat_path);
    if (!matfp) {
        LOG_ERROR << "Failed to open MAT file: " << mat_path;
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
        LOG_ERROR << "DIC3Dcombined structure not found in " << mat_path;
        return false;
    }

    auto ensure_loaded = [&](matvar_t* var, const char* label) {
        if (!var) {
            return false;
        }
        if (Mat_VarReadDataAll(matfp.get(), var) != 0) {
            if (!var->data && var->class_type != MAT_C_STRUCT && var->class_type != MAT_C_CELL) {
                LOG_WARN << "failed to fully load '" << label << "' from " << mat_path;
            }
        }
        return true;
    };

    // HDF5 (v7.3) reads defer nested field/cell data — historically each field
    // parser grew its own ensure_loaded call as it was found empty (Faces was
    // still missing one, making Step F see "0 faces" and abort). Force-load
    // the whole tree once instead of relying on per-field patchwork.
    std::function<void(matvar_t*)> deep_load = [&](matvar_t* v) {
        if (!v) return;
        Mat_VarReadDataAll(matfp.get(), v);
        size_t ne = 1;
        for (int i = 0; i < v->rank; ++i) ne *= v->dims[i];
        if (v->class_type == MAT_C_STRUCT) {
            unsigned nf = Mat_VarGetNumberOfFields(v);
            char* const* names = Mat_VarGetStructFieldnames(v);
            for (size_t e = 0; e < ne; ++e)
                for (unsigned i = 0; i < nf; ++i)
                    deep_load(Mat_VarGetStructFieldByName(v, names[i], e));
        } else if (v->class_type == MAT_C_CELL) {
            for (size_t e = 0; e < ne; ++e) deep_load(Mat_VarGetCell(v, static_cast<int>(e)));
        }
    };
    deep_load(dic3d_var.get());

    // Faces (Nx3) — handles both INT32 (C++-written) and DOUBLE (MATLAB-written)
    if (matvar_t* faces_var = getStructField(dic3d_var.get(), "Faces", 0)) {
        if (faces_var->data && faces_var->rank >= 2) {
            const size_t dim0 = faces_var->dims[0];
            const size_t dim1 = faces_var->dims[1];
            // Determine nFaces: dims could be {nFaces,3} or {3,nFaces}
            size_t nFaces = (dim1 == 3) ? dim0 : dim1;

            combined.Faces.clear();
            combined.Faces.reserve(nFaces * 3);

            if (faces_var->class_type == MAT_C_INT32) {
                // INT32: written by C++ writer (row-major, 1-indexed after fix)
                const int32_t* d = static_cast<const int32_t*>(faces_var->data);
                if (dim1 == 3) {
                    // {nFaces, 3} row-major layout from C++ writer
                    for (size_t i = 0; i < nFaces * 3; ++i) {
                        combined.Faces.push_back(d[i] - 1);
                    }
                } else {
                    // {3, nFaces} — transpose
                    for (size_t r = 0; r < nFaces; ++r) {
                        for (size_t c = 0; c < 3; ++c) {
                            combined.Faces.push_back(d[c * nFaces + r] - 1);
                        }
                    }
                }
            } else if (faces_var->class_type == MAT_C_DOUBLE) {
                // DOUBLE: written by MATLAB (column-major, 1-indexed)
                const double* d = static_cast<const double*>(faces_var->data);
                if (dim1 == 3) {
                    // {nFaces, 3} column-major: col0[nFaces], col1[nFaces], col2[nFaces]
                    for (size_t r = 0; r < nFaces; ++r) {
                        combined.Faces.push_back(static_cast<int>(d[r]) - 1);
                        combined.Faces.push_back(static_cast<int>(d[r + nFaces]) - 1);
                        combined.Faces.push_back(static_cast<int>(d[r + 2 * nFaces]) - 1);
                    }
                } else {
                    // {3, nFaces} column-major
                    for (size_t r = 0; r < nFaces; ++r) {
                        for (size_t c = 0; c < 3; ++c) {
                            combined.Faces.push_back(static_cast<int>(d[c + r * 3]) - 1);
                        }
                    }
                }
            } else {
                LOG_WARN << "Unexpected Faces class_type " << faces_var->class_type;
            }
            LOG_DEBUG << "Loaded " << nFaces << " faces (type=" << faces_var->class_type
                      << ", dims=" << dim0 << "x" << dim1 << ")";
        }
    }

    // Points3D (cell array: each cell is either
    //   - C++ format: 1x1 struct with fields x, y, z (each Nx1 double)
    //   - MATLAB format: Nx3 double matrix (column-major: col0=x, col1=y, col2=z)
    if (matvar_t* points3d_var = getStructField(dic3d_var.get(), "Points3D", 0)) {
        ensure_loaded(points3d_var, "Points3D");
        if (points3d_var->class_type == MAT_C_CELL) {
            const size_t nFrames = getCellArraySize(points3d_var);
            combined.Points3D.resize(nFrames);

            for (size_t frame = 0; frame < nFrames; ++frame) {
                matvar_t* frame_var = Mat_VarGetCell(points3d_var, frame);
                if (!frame_var) continue;
                ensure_loaded(frame_var, "Points3D cell");

                if (frame_var->class_type == MAT_C_STRUCT) {
                    // C++ format: struct with x, y, z fields
                    matvar_t* x_var = getStructField(frame_var, "x", 0);
                    matvar_t* y_var = getStructField(frame_var, "y", 0);
                    matvar_t* z_var = getStructField(frame_var, "z", 0);
                    ensure_loaded(x_var, "Points3D.x");
                    ensure_loaded(y_var, "Points3D.y");
                    ensure_loaded(z_var, "Points3D.z");

                    if (!x_var || !y_var || !z_var ||
                        x_var->class_type != MAT_C_DOUBLE || !x_var->data ||
                        y_var->class_type != MAT_C_DOUBLE || !y_var->data ||
                        z_var->class_type != MAT_C_DOUBLE || !z_var->data) {
                        continue;
                    }

                    combined.Points3D[frame].x = readDoubleArray(x_var);
                    combined.Points3D[frame].y = readDoubleArray(y_var);
                    combined.Points3D[frame].z = readDoubleArray(z_var);
                } else if (frame_var->class_type == MAT_C_DOUBLE && frame_var->data && frame_var->rank >= 2) {
                    const size_t dim0 = frame_var->dims[0];
                    const size_t dim1 = frame_var->dims[1];
                    const double* d = static_cast<const double*>(frame_var->data);
                    if (dim1 == 3) {
                        // {nPts, 3} column-major: col0=x[0..nPts-1], col1=y, col2=z
                        const size_t nPts = dim0;
                        combined.Points3D[frame].x.assign(d, d + nPts);
                        combined.Points3D[frame].y.assign(d + nPts, d + 2 * nPts);
                        combined.Points3D[frame].z.assign(d + 2 * nPts, d + 3 * nPts);
                    } else if (dim0 == 3) {
                        // {3, nPts} column-major: interleaved [x0,y0,z0,x1,y1,z1,...]
                        const size_t nPts = dim1;
                        combined.Points3D[frame].x.resize(nPts);
                        combined.Points3D[frame].y.resize(nPts);
                        combined.Points3D[frame].z.resize(nPts);
                        for (size_t p = 0; p < nPts; ++p) {
                            combined.Points3D[frame].x[p] = d[p * 3 + 0];
                            combined.Points3D[frame].y[p] = d[p * 3 + 1];
                            combined.Points3D[frame].z[p] = d[p * 3 + 2];
                        }
                    } else {
                        LOG_WARN << "Points3D frame " << frame << " has dims "
                                 << dim0 << "x" << dim1 << " (expected Nx3 or 3xN) in " << mat_path;
                        continue;
                    }
                } else {
                    continue;
                }

                // Consistency check
                if (combined.Points3D[frame].x.size() != combined.Points3D[frame].y.size() ||
                    combined.Points3D[frame].x.size() != combined.Points3D[frame].z.size()) {
                    LOG_WARN << "inconsistent x/y/z sizes in Points3D frame "
                             << frame << " in " << mat_path;

                    combined.Points3D[frame].x.clear();
                    combined.Points3D[frame].y.clear();
                    combined.Points3D[frame].z.clear();
                }
            }
            if (!combined.Points3D.empty() && !combined.Points3D[0].x.empty()) {
                LOG_DEBUG << "Loaded Points3D: " << nFrames << " frames, "
                          << combined.Points3D[0].x.size() << " points per frame";
            }
        }
    }

    // corrComb (cell array: each cell is Nx1 double vector, one per frame)
    if (matvar_t* corr_var = getStructField(dic3d_var.get(), "corrComb", 0)) {
        ensure_loaded(corr_var, "corrComb");
        if (corr_var->class_type == MAT_C_CELL) {
            const size_t nFrames = getCellArraySize(corr_var);
            combined.corrComb.resize(nFrames);
            for (size_t frame = 0; frame < nFrames; ++frame) {
                matvar_t* cell = Mat_VarGetCell(corr_var, frame);
                ensure_loaded(cell, "corrComb cell");
                if (cell && cell->class_type == MAT_C_DOUBLE && cell->data) {
                    combined.corrComb[frame] = readDoubleArray(cell);
                }
            }
        }
    }

    // FaceCorrComb (cell array: each cell is Mx1 double vector, one per frame)
    if (matvar_t* face_corr_var = getStructField(dic3d_var.get(), "FaceCorrComb", 0)) {
        if (face_corr_var->class_type == MAT_C_CELL) {
            const size_t nFrames = getCellArraySize(face_corr_var);
            combined.FaceCorrComb.resize(nFrames);
            for (size_t frame = 0; frame < nFrames; ++frame) {
                matvar_t* cell = Mat_VarGetCell(face_corr_var, frame);
                if (cell && cell->class_type == MAT_C_DOUBLE && cell->data) {
                    combined.FaceCorrComb[frame] = readDoubleArray(cell);
                }
            }
        }
    }

    // FaceCentroids (cell array: each cell is Mx3 or 3xM double)
    // Internal format: interleaved [x0,y0,z0, x1,y1,z1, ...]
    if (matvar_t* fc_var = getStructField(dic3d_var.get(), "FaceCentroids", 0)) {
        if (fc_var->class_type == MAT_C_CELL) {
            const size_t nFrames = getCellArraySize(fc_var);
            combined.FaceCentroids.resize(nFrames);
            for (size_t frame = 0; frame < nFrames; ++frame) {
                matvar_t* cell = Mat_VarGetCell(fc_var, frame);
                if (cell && cell->class_type == MAT_C_DOUBLE && cell->data && cell->rank >= 2) {
                    size_t dim0 = cell->dims[0];
                    size_t dim1 = cell->dims[1];
                    const double* d = static_cast<const double*>(cell->data);
                    if (dim0 == 3) {
                        // {3, nFaces} column-major: already interleaved
                        combined.FaceCentroids[frame].assign(d, d + dim0 * dim1);
                    } else if (dim1 == 3) {
                        // {nFaces, 3} column-major: transpose to interleaved
                        size_t nFaces = dim0;
                        combined.FaceCentroids[frame].resize(nFaces * 3);
                        for (size_t f = 0; f < nFaces; ++f) {
                            combined.FaceCentroids[frame][f*3+0] = d[f];
                            combined.FaceCentroids[frame][f*3+1] = d[f + nFaces];
                            combined.FaceCentroids[frame][f*3+2] = d[f + 2*nFaces];
                        }
                    } else {
                        combined.FaceCentroids[frame].assign(d, d + dim0 * dim1);
                    }
                }
            }
        }
    }

    // Disp (struct with DispVec and DispMgn cell arrays)
    if (matvar_t* disp_var = getStructField(dic3d_var.get(), "Disp", 0)) {
        if (disp_var->class_type == MAT_C_STRUCT) {
            // DispVec: cell array, each cell is Nx3 or 3xN double
            // Internal format: interleaved [dx0,dy0,dz0, dx1,dy1,dz1, ...]
            if (matvar_t* dv_var = getStructField(disp_var, "DispVec", 0)) {
                if (dv_var->class_type == MAT_C_CELL) {
                    const size_t nFrames = getCellArraySize(dv_var);
                    combined.Disp.DispVec.resize(nFrames);
                    for (size_t frame = 0; frame < nFrames; ++frame) {
                        matvar_t* cell = Mat_VarGetCell(dv_var, frame);
                        if (cell && cell->class_type == MAT_C_DOUBLE && cell->data && cell->rank >= 2) {
                            size_t dim0 = cell->dims[0];
                            size_t dim1 = cell->dims[1];
                            const double* d = static_cast<const double*>(cell->data);
                            if (dim0 == 3) {
                                // {3, nPts} column-major: already interleaved
                                combined.Disp.DispVec[frame].assign(d, d + dim0 * dim1);
                            } else if (dim1 == 3) {
                                // {nPts, 3} column-major: transpose to interleaved
                                size_t nPts = dim0;
                                combined.Disp.DispVec[frame].resize(nPts * 3);
                                for (size_t p = 0; p < nPts; ++p) {
                                    combined.Disp.DispVec[frame][p*3+0] = d[p];
                                    combined.Disp.DispVec[frame][p*3+1] = d[p + nPts];
                                    combined.Disp.DispVec[frame][p*3+2] = d[p + 2*nPts];
                                }
                            } else {
                                combined.Disp.DispVec[frame].assign(d, d + dim0 * dim1);
                            }
                        }
                    }
                }
            }
            // DispMgn: cell array, each cell is Nx1 double vector
            if (matvar_t* dm_var = getStructField(disp_var, "DispMgn", 0)) {
                if (dm_var->class_type == MAT_C_CELL) {
                    const size_t nFrames = getCellArraySize(dm_var);
                    combined.Disp.DispMgn.resize(nFrames);
                    for (size_t frame = 0; frame < nFrames; ++frame) {
                        matvar_t* cell = Mat_VarGetCell(dm_var, frame);
                        if (cell && cell->class_type == MAT_C_DOUBLE && cell->data) {
                            combined.Disp.DispMgn[frame] = readDoubleArray(cell);
                        }
                    }
                }
            }
        }
    }

    // FacePairInds / PointPairInds (handles INT32 or DOUBLE, any dim order)
    if (matvar_t* facepair_var = getStructField(dic3d_var.get(), "FacePairInds", 0)) {
        combined.FacePairInds = readIntArray(facepair_var);
    }

    if (matvar_t* pointpair_var = getStructField(dic3d_var.get(), "PointPairInds", 0)) {
        combined.PointPairInds = readIntArray(pointpair_var);
    }

    if (matvar_t* dic2d_info_var = getStructField(dic3d_var.get(), "DIC2Dinfo", 0)) {
        if (dic2d_info_var->class_type == MAT_C_CELL) {
            const size_t nEntries = getCellArraySize(dic2d_info_var);
            combined.DIC2Dinfo.clear();
            combined.DIC2Dinfo.resize(nEntries);
            for (size_t i = 0; i < nEntries; ++i) {
                matvar_t* cell = Mat_VarGetCell(dic2d_info_var, i);
                if (cell && cell->class_type == MAT_C_STRUCT) {
                    readDIC2DPairResultsFromStruct(cell, combined.DIC2Dinfo[i]);
                }
            }
        }
    }

    LOG_INFO << "Loaded DIC3Dcombined: "
             << combined.Points3D.size() << " frames, "
             << (combined.Points3D.empty() ? 0 : combined.Points3D[0].x.size()) << " points, "
             << combined.Faces.size() / 3 << " faces";

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

bool MatReader::readDIC2DPairResultsFromStruct(matvar_t* dic2d_root, DIC2DPairResults& result) {
    if (!dic2d_root || dic2d_root->class_type != MAT_C_STRUCT) {
        return false;
    }

    result = DIC2DPairResults{};

    readStructInt(dic2d_root, "nCamRef", result.nCamRef);
    readStructInt(dic2d_root, "nCamDef", result.nCamDef);
    readStructInt(dic2d_root, "nImages", result.nImages);
    if (matvar_t* pair_order_var = getStructField(dic2d_root, "pairOrder", 0)) {
        result.pairOrder = readIntArray(pair_order_var);
    } else {
        result.pairOrder.clear();
    }
    readStructBool(dic2d_root, "pairForced", result.pairForced);

    if (matvar_t* roi_var = getStructField(dic2d_root, "ROImask", 0)) {
        result.ROImask = readImage(roi_var);
    }

    if (matvar_t* faces_var = getStructField(dic2d_root, "Faces", 0)) {
        if (faces_var->data && faces_var->rank >= 2) {
            const size_t rows = faces_var->dims[0];
            const size_t cols = faces_var->dims[1];
            const size_t nFaces = (cols == 3) ? rows : ((rows == 3) ? cols : 0);

            result.Faces.clear();
            if (nFaces > 0) {
                result.Faces.reserve(nFaces * 3);

                auto pushFaces = [&](auto* dataPtr) {
                    if (cols == 3) {
                        for (size_t f = 0; f < nFaces; ++f) {
                            result.Faces.push_back(static_cast<int>(dataPtr[f]) - 1);
                            result.Faces.push_back(static_cast<int>(dataPtr[f + nFaces]) - 1);
                            result.Faces.push_back(static_cast<int>(dataPtr[f + 2 * nFaces]) - 1);
                        }
                    } else {
                        for (size_t f = 0; f < nFaces; ++f) {
                            result.Faces.push_back(static_cast<int>(dataPtr[f * 3 + 0]) - 1);
                            result.Faces.push_back(static_cast<int>(dataPtr[f * 3 + 1]) - 1);
                            result.Faces.push_back(static_cast<int>(dataPtr[f * 3 + 2]) - 1);
                        }
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
                }
            }
        }
    }

    if (matvar_t* facecolors_var = getStructField(dic2d_root, "FaceColors", 0)) {
        result.FaceColors = readDoubleArray(facecolors_var);
    }

    if (matvar_t* points_var = getStructField(dic2d_root, "Points", 0)) {
        if (points_var->class_type == MAT_C_CELL) {
            const size_t nFrames = getCellArraySize(points_var);
            result.Points.resize(nFrames);
            for (size_t frame = 0; frame < nFrames; ++frame) {
                matvar_t* frame_var = Mat_VarGetCell(points_var, frame);
                if (!frame_var || frame_var->class_type != MAT_C_DOUBLE || !frame_var->data || frame_var->rank < 2) {
                    continue;
                }

                const size_t dim0 = frame_var->dims[0];
                const size_t dim1 = frame_var->dims[1];
                const double* pts_data = static_cast<const double*>(frame_var->data);

                if (dim1 == 2) {
                    const size_t nPoints = dim0;
                    result.Points[frame].x.resize(nPoints);
                    result.Points[frame].y.resize(nPoints);
                    for (size_t i = 0; i < nPoints; ++i) {
                        result.Points[frame].x[i] = pts_data[i];
                        result.Points[frame].y[i] = pts_data[i + nPoints];
                    }
                } else if (dim0 == 2) {
                    const size_t nPoints = dim1;
                    result.Points[frame].x.resize(nPoints);
                    result.Points[frame].y.resize(nPoints);
                    for (size_t i = 0; i < nPoints; ++i) {
                        result.Points[frame].x[i] = pts_data[i * 2 + 0];
                        result.Points[frame].y[i] = pts_data[i * 2 + 1];
                    }
                }
            }
        }
    }

    if (matvar_t* corr_var = getStructField(dic2d_root, "CorCoeffVec", 0)) {
        if (corr_var->class_type == MAT_C_CELL) {
            const size_t nFrames = getCellArraySize(corr_var);
            result.CorCoeffVec.resize(nFrames);
            for (size_t frame = 0; frame < nFrames; ++frame) {
                matvar_t* frame_var = Mat_VarGetCell(corr_var, frame);
                if (frame_var && frame_var->class_type == MAT_C_DOUBLE && frame_var->data) {
                    result.CorCoeffVec[frame] = readDoubleArray(frame_var);
                }
            }
        }
    }

    if (matvar_t* ncorr_var = getStructField(dic2d_root, "ncorrInfo", 0)) {
        if (ncorr_var->class_type == MAT_C_STRUCT) {
            if (matvar_t* cutoff_cc = getStructField(ncorr_var, "cutoff_corrcoef", 0)) {
                result.ncorrInfo.cutoff_corrcoef = readDoubleArray(cutoff_cc);
            }

            readStructDouble(ncorr_var, "cutoff_diffnorm", result.ncorrInfo.cutoff_diffnorm);
            readStructInt(ncorr_var, "cutoff_iteration", result.ncorrInfo.cutoff_iteration);
            readStructInt(ncorr_var, "radius", result.ncorrInfo.radius);
            readStructInt(ncorr_var, "spacing", result.ncorrInfo.spacing);
            readStructDouble(ncorr_var, "pixtounits", result.ncorrInfo.pixtounits);
            readStructInt(ncorr_var, "lenscoef", result.ncorrInfo.lenscoef);
            readStructBool(ncorr_var, "subsettrunc", result.ncorrInfo.subsettrunc);
            readStructInt(ncorr_var, "total_threads", result.ncorrInfo.total_threads);
            readStructString(ncorr_var, "type", result.ncorrInfo.type);
            readStructString(ncorr_var, "units", result.ncorrInfo.units);

            matvar_t* step_var = getStructField(ncorr_var, "stepanalysis", 0);
            if (step_var && step_var->class_type == MAT_C_STRUCT) {
                readStructBool(step_var, "enabled", result.ncorrInfo.stepanalysis.enabled);
                readStructString(step_var, "type", result.ncorrInfo.stepanalysis.type);
                readStructBool(step_var, "auto", result.ncorrInfo.stepanalysis.auto_update);
                readStructInt(step_var, "step", result.ncorrInfo.stepanalysis.step);
            }
        }
    }

    return true;
}

bool MatReader::readDIC2DPairResults(const std::string& mat_path, DIC2DPairResults& result) {
    auto matfp = openMat(mat_path);
    if (!matfp) {
        LOG_ERROR << "Failed to open MAT file: " << mat_path;
        return false;
    }

    result = DIC2DPairResults{};

    auto first_var = readVar(matfp.get(), "DIC2DpairResults");
    matvar_t* dic2d_root = nullptr;
    if (first_var && first_var->name && std::strcmp(first_var->name, "DIC2DpairResults") == 0 &&
        first_var->class_type == MAT_C_STRUCT) {
        dic2d_root = first_var.get();
    }

    if (dic2d_root) {
        if (!readDIC2DPairResultsFromStruct(dic2d_root, result)) {
            return false;
        }
        LOG_INFO << "Loaded DIC2DPairResults: "
                 << "nCamRef=" << result.nCamRef
                 << ", nCamDef=" << result.nCamDef
                 << ", nImages=" << result.nImages
                 << ", pairOrder=" << result.pairOrder.size()
                 << ", " << result.Points.size() << " frames"
                 << ", " << result.Faces.size() / 3 << " faces";
        return true;
    }

    std::vector<MatVarPtr> owned_vars;
    owned_vars.reserve(10);

    auto readAnyVar = [&](const char* name) -> matvar_t* {
        if (dic2d_root) {
            return getStructField(dic2d_root, name, 0);
        }
        owned_vars.push_back(readVar(matfp.get(), name));
        return owned_vars.back().get();
    };

    auto readAnyInt = [&](const char* name, int& out) {
        if (dic2d_root) {
            readStructInt(dic2d_root, name, out);
        } else {
            readTopInt(matfp.get(), name, out);
        }
    };

    readAnyInt("nCamRef", result.nCamRef);
    readAnyInt("nCamDef", result.nCamDef);
    readAnyInt("nImages", result.nImages);
    if (matvar_t* pair_order_var = readAnyVar("pairOrder")) {
        result.pairOrder = readIntArray(pair_order_var);
    } else {
        result.pairOrder.clear();
    }
    if (matvar_t* pair_forced_var = readAnyVar("pairForced")) {
        result.pairForced = (readScalar(pair_forced_var) != 0.0);
    } else {
        result.pairForced = false;
    }

    if (matvar_t* roi_var = readAnyVar("ROImask")) {
        result.ROImask = readImage(roi_var);
    }

    if (matvar_t* faces_var = readAnyVar("Faces")) {
        if (faces_var->data && faces_var->rank >= 2) {
            const size_t rows = faces_var->dims[0];
            const size_t cols = faces_var->dims[1];
            const size_t nFaces = (cols == 3) ? rows : ((rows == 3) ? cols : 0);

            result.Faces.clear();

            if (nFaces == 0) {
                LOG_WARN << "Faces has unexpected shape (" << rows << "x" << cols
                         << "), expected Nx3 or 3xN in " << mat_path;
            } else {
                result.Faces.reserve(nFaces * 3);

                auto pushFaces = [&](auto* dataPtr) {
                    if (cols == 3) {
                        // MATLAB Nx3 faces are stored column-major: [all v0, all v1, all v2].
                        for (size_t f = 0; f < nFaces; ++f) {
                            result.Faces.push_back(static_cast<int>(dataPtr[f]) - 1);
                            result.Faces.push_back(static_cast<int>(dataPtr[f + nFaces]) - 1);
                            result.Faces.push_back(static_cast<int>(dataPtr[f + 2 * nFaces]) - 1);
                        }
                    } else {
                        // C++ writer uses 3xN, which is already interleaved face-by-face in MAT storage.
                        for (size_t f = 0; f < nFaces; ++f) {
                            result.Faces.push_back(static_cast<int>(dataPtr[f * 3 + 0]) - 1);
                            result.Faces.push_back(static_cast<int>(dataPtr[f * 3 + 1]) - 1);
                            result.Faces.push_back(static_cast<int>(dataPtr[f * 3 + 2]) - 1);
                        }
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
                    LOG_WARN << "Faces has unsupported type (class_type=" << faces_var->class_type
                             << ") in " << mat_path;
                }
            }
        }
    }

    if (matvar_t* facecolors_var = readAnyVar("FaceColors")) {
        result.FaceColors = readDoubleArray(facecolors_var);
    }

    if (matvar_t* points_var = readAnyVar("Points")) {
        if (points_var->class_type == MAT_C_CELL) {
            const size_t nFrames = getCellArraySize(points_var);
            result.Points.resize(nFrames);

            for (size_t frame = 0; frame < nFrames; ++frame) {
                matvar_t* frame_var = Mat_VarGetCell(points_var, frame);
                if (!frame_var || frame_var->class_type != MAT_C_DOUBLE || !frame_var->data || frame_var->rank < 2)
                    continue;

                const size_t dim0 = frame_var->dims[0];
                const size_t dim1 = frame_var->dims[1];
                const double* pts_data = static_cast<const double*>(frame_var->data);

                if (dim1 == 2) {
                    // {nPoints, 2} column-major: x=col0, y=col1
                    const size_t nPoints = dim0;
                    result.Points[frame].x.resize(nPoints);
                    result.Points[frame].y.resize(nPoints);
                    for (size_t i = 0; i < nPoints; ++i) {
                        result.Points[frame].x[i] = pts_data[i];
                        result.Points[frame].y[i] = pts_data[i + nPoints];
                    }
                } else if (dim0 == 2) {
                    // {2, nPoints} column-major: interleaved [x0,y0,x1,y1,...]
                    const size_t nPoints = dim1;
                    result.Points[frame].x.resize(nPoints);
                    result.Points[frame].y.resize(nPoints);
                    for (size_t i = 0; i < nPoints; ++i) {
                        result.Points[frame].x[i] = pts_data[i * 2 + 0];
                        result.Points[frame].y[i] = pts_data[i * 2 + 1];
                    }
                }
            }
        }
    }

    if (matvar_t* corr_var = readAnyVar("CorCoeffVec")) {
        if (corr_var->class_type == MAT_C_CELL) {
            const size_t nFrames = getCellArraySize(corr_var);
            result.CorCoeffVec.resize(nFrames);

            for (size_t frame = 0; frame < nFrames; ++frame) {
                matvar_t* frame_var = Mat_VarGetCell(corr_var, frame);
                if (frame_var && frame_var->class_type == MAT_C_DOUBLE && frame_var->data) {
                    result.CorCoeffVec[frame] = readDoubleArray(frame_var);
                }
            }
        }
    }

    if (matvar_t* ncorr_var = readAnyVar("ncorrInfo")) {
        if (ncorr_var->class_type == MAT_C_STRUCT) {
            if (matvar_t* cutoff_cc = getStructField(ncorr_var, "cutoff_corrcoef", 0)) {
                result.ncorrInfo.cutoff_corrcoef = readDoubleArray(cutoff_cc);
            }

            readStructDouble(ncorr_var, "cutoff_diffnorm", result.ncorrInfo.cutoff_diffnorm);
            readStructInt(ncorr_var, "cutoff_iteration", result.ncorrInfo.cutoff_iteration);
            readStructInt(ncorr_var, "radius", result.ncorrInfo.radius);
            readStructInt(ncorr_var, "spacing", result.ncorrInfo.spacing);
            readStructDouble(ncorr_var, "pixtounits", result.ncorrInfo.pixtounits);
            readStructInt(ncorr_var, "lenscoef", result.ncorrInfo.lenscoef);
            readStructBool(ncorr_var, "subsettrunc", result.ncorrInfo.subsettrunc);
            readStructInt(ncorr_var, "total_threads", result.ncorrInfo.total_threads);
            readStructString(ncorr_var, "type", result.ncorrInfo.type);
            readStructString(ncorr_var, "units", result.ncorrInfo.units);

            matvar_t* step_var = getStructField(ncorr_var, "stepanalysis", 0);
            if (step_var && step_var->class_type == MAT_C_STRUCT) {
                readStructBool(step_var, "enabled", result.ncorrInfo.stepanalysis.enabled);
                readStructString(step_var, "type", result.ncorrInfo.stepanalysis.type);
                readStructBool(step_var, "auto", result.ncorrInfo.stepanalysis.auto_update);
                readStructInt(step_var, "step", result.ncorrInfo.stepanalysis.step);
            }
        }
    }

    LOG_INFO << "Loaded DIC2DPairResults: "
             << "nCamRef=" << result.nCamRef
             << ", nCamDef=" << result.nCamDef
             << ", nImages=" << result.nImages
             << ", pairOrder=" << result.pairOrder.size()
             << ", " << result.Points.size() << " frames"
             << ", " << result.Faces.size() / 3 << " faces";

    return true;
}

} // namespace cppxdic
