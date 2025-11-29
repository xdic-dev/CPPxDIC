/**
 * MAT File Reader Implementation
 */

#include "mat_reader.h"
#include "dic_structures.h"
#include <iostream>
#include <sstream>
#include <cstring>

namespace cppxdic {

matvar_t* MatReader::readStructField(mat_t* matfp, 
                                     const std::string& struct_name,
                                     const std::string& field_name) {
    if (!matfp) return nullptr;
    
    // Read struct variable
    matvar_t* struct_var = Mat_VarRead(matfp, struct_name.c_str());
    if (!struct_var || struct_var->class_type != MAT_C_STRUCT) {
        if (struct_var) Mat_VarFree(struct_var);
        return nullptr;
    }
    
    // Get field
    matvar_t* field = Mat_VarGetStructFieldByName(struct_var, field_name.c_str(), 0);
    matvar_t* result = nullptr;
    if (field) {
        result = Mat_VarDuplicate(field, 1);  // Deep copy
    }
    
    Mat_VarFree(struct_var);
    return result;
}

matvar_t* MatReader::readNestedField(mat_t* matfp, const std::string& path) {
    if (!matfp) return nullptr;
    
    // Split path by '.'
    std::vector<std::string> parts;
    std::istringstream iss(path);
    std::string part;
    while (std::getline(iss, part, '.')) {
        parts.push_back(part);
    }
    
    if (parts.empty()) return nullptr;
    
    // Read root variable
    matvar_t* current = Mat_VarRead(matfp, parts[0].c_str());
    if (!current) return nullptr;
    
    // Navigate through nested fields
    for (size_t i = 1; i < parts.size(); ++i) {
        if (current->class_type != MAT_C_STRUCT) {
            Mat_VarFree(current);
            return nullptr;
        }
        
        matvar_t* field = Mat_VarGetStructFieldByName(current, parts[i].c_str(), 0);
        if (!field) {
            Mat_VarFree(current);
            return nullptr;
        }
        
        matvar_t* temp = Mat_VarDuplicate(field, 1);
        Mat_VarFree(current);
        current = temp;
    }
    
    return current;
}

std::vector<double> MatReader::readDoubleArray(matvar_t* var) {
    std::vector<double> result;
    if (!var || !var->data) return result;
    
    if (var->class_type != MAT_C_DOUBLE) {
        std::cerr << "Warning: Variable is not double type" << std::endl;
        return result;
    }
    
    size_t n_elements = 1;
    for (int i = 0; i < var->rank; ++i) {
        n_elements *= var->dims[i];
    }
    
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
        std::cerr << "Warning: Variable is not double type" << std::endl;
        return result;
    }
    
    rows = var->dims[0];
    cols = var->dims[1];
    
    const double* data = static_cast<const double*>(var->data);
    size_t n_elements = rows * cols;
    
    // MATLAB stores in column-major, convert to row-major
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
        std::cerr << "Warning: Expected 2D array for image" << std::endl;
        return cv::Mat();
    }
    
    size_t rows = var->dims[0];
    size_t cols = var->dims[1];
    
    cv::Mat img;
    
    if (var->class_type == MAT_C_DOUBLE) {
        const double* data = static_cast<const double*>(var->data);
        img = cv::Mat(rows, cols, CV_64F);
        
        // Convert from column-major (MATLAB) to row-major (OpenCV)
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                img.at<double>(r, c) = data[c * rows + r];
            }
        }
    } else if (var->class_type == MAT_C_UINT8) {
        const uint8_t* data = static_cast<const uint8_t*>(var->data);
        img = cv::Mat(rows, cols, CV_8U);
        
        // Check if this is a MATLAB logical array (values are 0 and 1)
        bool is_logical = (var->isLogical != 0);
        
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                uint8_t value = data[c * rows + r];
                // Scale logical values (0,1) to image values (0,255) for proper visualization and processing
                img.at<uint8_t>(r, c) = is_logical ? (value * 255) : value;
            }
        }
    } else {
        std::cerr << "Warning: Unsupported image data type" << std::endl;
        return cv::Mat();
    }
    
    return img;
}

double MatReader::readScalar(matvar_t* var) {
    if (!var || !var->data) return 0.0;
    
    if (var->class_type == MAT_C_DOUBLE) {
        const double* data = static_cast<const double*>(var->data);
        return data[0];
    } else if (var->class_type == MAT_C_INT32) {
        const int32_t* data = static_cast<const int32_t*>(var->data);
        return static_cast<double>(data[0]);
    } else if (var->class_type == MAT_C_UINT8) {
        const uint8_t* data = static_cast<const uint8_t*>(var->data);
        return static_cast<double>(data[0]);
    }
    
    std::cerr << "Warning: Unsupported scalar type" << std::endl;
    return 0.0;
}

std::string MatReader::readString(matvar_t* var) {
    if (!var || !var->data) return "";
    
    if (var->class_type == MAT_C_CHAR) {
        const char* data = static_cast<const char*>(var->data);
        size_t len = 1;
        for (int i = 0; i < var->rank; ++i) {
            len *= var->dims[i];
        }
        return std::string(data, len);
    } else if (var->class_type == MAT_C_UINT16) {
        // MATLAB often stores strings as uint16
        const uint16_t* data = static_cast<const uint16_t*>(var->data);
        size_t len = 1;
        for (int i = 0; i < var->rank; ++i) {
            len *= var->dims[i];
        }
        std::string result;
        for (size_t i = 0; i < len; ++i) {
            result += static_cast<char>(data[i]);
        }
        return result;
    }
    
    std::cerr << "Warning: Unsupported string type" << std::endl;
    return "";
}

size_t MatReader::getCellArraySize(matvar_t* var) {
    if (!var || var->class_type != MAT_C_CELL) return 0;
    
    size_t n_cells = 1;
    for (int i = 0; i < var->rank; ++i) {
        n_cells *= var->dims[i];
    }
    
    return n_cells;
}

matvar_t* MatReader::getCellElement(matvar_t* var, size_t index) {
    if (!var || var->class_type != MAT_C_CELL) return nullptr;
    
    size_t n_cells = getCellArraySize(var);
    if (index >= n_cells) return nullptr;
    
    matvar_t** cells = static_cast<matvar_t**>(var->data);
    return cells[index];
}

std::map<std::string, double> MatReader::readDispInfo(mat_t* matfp) {
    std::map<std::string, double> params;
    
    if (!matfp) return params;
    
    // Try to read from data_dic_save.dispinfo
    matvar_t* dispinfo = readNestedField(matfp, "data_dic_save.dispinfo");
    if (!dispinfo || dispinfo->class_type != MAT_C_STRUCT) {
        if (dispinfo) Mat_VarFree(dispinfo);
        return params;
    }
    
    // Read common dispinfo fields
    std::vector<std::string> field_names = {
        "cutoff_corrcoef", "cutoff_diffnorm", "cutoff_iteration",
        "radius", "spacing", "subsettrunc", "total_threads"
    };
    
    for (const auto& field : field_names) {
        matvar_t* field_var = Mat_VarGetStructFieldByName(dispinfo, field.c_str(), 0);
        if (field_var && field_var->data) {
            double value = readScalar(field_var);
            params[field] = value;
        }
    }
    
    Mat_VarFree(dispinfo);
    return params;
}

bool MatReader::variableExists(mat_t* matfp, const std::string& var_name) {
    if (!matfp) return false;
    
    matvar_t* var = Mat_VarRead(matfp, var_name.c_str());
    if (var) {
        Mat_VarFree(var);
        return true;
    }
    
    return false;
}

cv::Mat MatReader::readROIMask(const std::string& mat_path, const std::string& var_name) {
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open MAT file: " << mat_path << std::endl;
        return cv::Mat();
    }
    
    matvar_t* var = Mat_VarRead(matfp, var_name.c_str());
    cv::Mat mask;
    
    if (var) {
        mask = readImage(var);
        Mat_VarFree(var);
    } else {
        std::cerr << "Variable '" << var_name << "' not found in " << mat_path << std::endl;
    }
    
    Mat_Close(matfp);
    return mask;
}

bool MatReader::readDIC3Dcombined(const std::string& mat_path, DIC3Dcombined& combined) {
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open MAT file: " << mat_path << std::endl;
        return false;
    }
    
    // Read the main structure (usually just the variable itself, not nested)
    matvar_t* dic3d_var = Mat_VarRead(matfp, nullptr);  // Read first variable
    if (!dic3d_var) {
        // Try common variable names
        const char* var_names[] = {"DIC3Dcombined", "DIC3D", "data"};
        for (const char* name : var_names) {
            dic3d_var = Mat_VarRead(matfp, name);
            if (dic3d_var) break;
        }
    }
    
    if (!dic3d_var || dic3d_var->class_type != MAT_C_STRUCT) {
        std::cerr << "DIC3Dcombined structure not found in " << mat_path << std::endl;
        if (dic3d_var) Mat_VarFree(dic3d_var);
        Mat_Close(matfp);
        return false;
    }
    
    // Read Faces (Nx3 matrix)
    matvar_t* faces_var = Mat_VarGetStructFieldByName(dic3d_var, "Faces", 0);
    if (faces_var && faces_var->class_type == MAT_C_DOUBLE) {
        size_t nFaces = faces_var->dims[0];
        const double* faces_data = static_cast<const double*>(faces_var->data);
        combined.Faces.clear();
        combined.Faces.reserve(nFaces * 3);
        for (size_t i = 0; i < nFaces * 3; ++i) {
            combined.Faces.push_back(static_cast<int>(faces_data[i]) - 1);  // MATLAB 1-indexed to C++ 0-indexed
        }
    }
    
    // Read Points3D (cell array of frames)
    matvar_t* points3d_var = Mat_VarGetStructFieldByName(dic3d_var, "Points3D", 0);
    if (points3d_var && points3d_var->class_type == MAT_C_CELL) {
        size_t nFrames = points3d_var->dims[0] * points3d_var->dims[1];
        combined.Points3D.resize(nFrames);
        
        for (size_t frame = 0; frame < nFrames; ++frame) {
            matvar_t* frame_var = Mat_VarGetCell(points3d_var, frame);
            if (frame_var && frame_var->class_type == MAT_C_DOUBLE) {
                size_t nPoints = frame_var->dims[0];
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
    
    // Read FacePairInds
    matvar_t* facepair_var = Mat_VarGetStructFieldByName(dic3d_var, "FacePairInds", 0);
    if (facepair_var && facepair_var->class_type == MAT_C_DOUBLE) {
        size_t n = facepair_var->dims[0];
        const double* data = static_cast<const double*>(facepair_var->data);
        combined.FacePairInds.assign(data, data + n);
    }
    
    // Read PointPairInds
    matvar_t* pointpair_var = Mat_VarGetStructFieldByName(dic3d_var, "PointPairInds", 0);
    if (pointpair_var && pointpair_var->class_type == MAT_C_DOUBLE) {
        size_t n = pointpair_var->dims[0];
        const double* data = static_cast<const double*>(pointpair_var->data);
        combined.PointPairInds.assign(data, data + n);
    }
    
    std::cout << "Loaded DIC3Dcombined: " 
              << combined.Points3D.size() << " frames, "
              << (combined.Points3D.empty() ? 0 : combined.Points3D[0].x.size()) << " points, "
              << combined.Faces.size() / 3 << " faces" << std::endl;
    
    Mat_VarFree(dic3d_var);
    Mat_Close(matfp);
    return true;
}

std::vector<int> MatReader::readIntArray(matvar_t* var) {
    std::vector<int> result;
    if (!var || !var->data) return result;
    
    size_t n_elements = 1;
    for (int i = 0; i < var->rank; ++i) {
        n_elements *= var->dims[i];
    }
    
    if (var->class_type == MAT_C_DOUBLE) {
        const double* data = static_cast<const double*>(var->data);
        result.reserve(n_elements);
        for (size_t i = 0; i < n_elements; ++i) {
            result.push_back(static_cast<int>(data[i]));
        }
    } else if (var->class_type == MAT_C_INT32) {
        const int32_t* data = static_cast<const int32_t*>(var->data);
        result.assign(data, data + n_elements);
    } else if (var->class_type == MAT_C_UINT16) {
        const uint16_t* data = static_cast<const uint16_t*>(var->data);
        result.reserve(n_elements);
        for (size_t i = 0; i < n_elements; ++i) {
            result.push_back(static_cast<int>(data[i]));
        }
    } else if (var->class_type == MAT_C_UINT32) {
        const uint32_t* data = static_cast<const uint32_t*>(var->data);
        result.reserve(n_elements);
        for (size_t i = 0; i < n_elements; ++i) {
            result.push_back(static_cast<int>(data[i]));
        }
    }
    
    return result;
}

bool MatReader::readDIC2DPairResults(const std::string& mat_path, DIC2DPairResults& result) {
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open MAT file: " << mat_path << std::endl;
        return false;
    }
    
    // Read the main structure - try common variable names
    matvar_t* dic2d_var = nullptr;
    const char* var_names[] = {"DIC2DpairResults", "DIC2D", "data"};
    for (const char* name : var_names) {
        dic2d_var = Mat_VarRead(matfp, name);
        if (dic2d_var) break;
    }
    
    if (!dic2d_var || dic2d_var->class_type != MAT_C_STRUCT) {
        std::cerr << "DIC2DpairResults structure not found in " << mat_path << std::endl;
        if (dic2d_var) Mat_VarFree(dic2d_var);
        Mat_Close(matfp);
        return false;
    }
    
    // Read scalar fields: nCamRef, nCamDef, nImages
    matvar_t* nCamRef_var = Mat_VarGetStructFieldByName(dic2d_var, "nCamRef", 0);
    if (nCamRef_var) {
        result.nCamRef = static_cast<int>(readScalar(nCamRef_var));
    }
    
    matvar_t* nCamDef_var = Mat_VarGetStructFieldByName(dic2d_var, "nCamDef", 0);
    if (nCamDef_var) {
        result.nCamDef = static_cast<int>(readScalar(nCamDef_var));
    }
    
    matvar_t* nImages_var = Mat_VarGetStructFieldByName(dic2d_var, "nImages", 0);
    if (nImages_var) {
        result.nImages = static_cast<int>(readScalar(nImages_var));
    }
    
    // Read ROImask (uint8 2D array)
    matvar_t* roi_var = Mat_VarGetStructFieldByName(dic2d_var, "ROImask", 0);
    if (roi_var) {
        result.ROImask = readImage(roi_var);
    }
    
    // Read Faces (Nx3 or 3xN array)
    matvar_t* faces_var = Mat_VarGetStructFieldByName(dic2d_var, "Faces", 0);
    if (faces_var && faces_var->data) {
        size_t rows = faces_var->dims[0];
        size_t cols = faces_var->dims[1];
        size_t nFaces = (cols == 3) ? rows : cols;  // Handle both orientations
        
        result.Faces.clear();
        result.Faces.reserve(nFaces * 3);
        
        if (faces_var->class_type == MAT_C_DOUBLE) {
            const double* data = static_cast<const double*>(faces_var->data);
            for (size_t i = 0; i < nFaces * 3; ++i) {
                result.Faces.push_back(static_cast<int>(data[i]) - 1);  // MATLAB 1-indexed to 0-indexed
            }
        } else if (faces_var->class_type == MAT_C_UINT16) {
            const uint16_t* data = static_cast<const uint16_t*>(faces_var->data);
            for (size_t i = 0; i < nFaces * 3; ++i) {
                result.Faces.push_back(static_cast<int>(data[i]) - 1);
            }
        }
    }
    
    // Read FaceColors (1D array)
    matvar_t* facecolors_var = Mat_VarGetStructFieldByName(dic2d_var, "FaceColors", 0);
    if (facecolors_var) {
        result.FaceColors = readDoubleArray(facecolors_var);
    }
    
    // Read Points (cell array: each cell is Nx2 array)
    matvar_t* points_var = Mat_VarGetStructFieldByName(dic2d_var, "Points", 0);
    if (points_var && points_var->class_type == MAT_C_CELL) {
        size_t nFrames = getCellArraySize(points_var);
        result.Points.resize(nFrames);
        
        for (size_t frame = 0; frame < nFrames; ++frame) {
            matvar_t* frame_var = getCellElement(points_var, frame);
            if (frame_var && frame_var->class_type == MAT_C_DOUBLE && frame_var->data) {
                size_t nPoints = frame_var->dims[0];
                const double* pts_data = static_cast<const double*>(frame_var->data);
                
                result.Points[frame].x.resize(nPoints);
                result.Points[frame].y.resize(nPoints);
                
                // MATLAB stores column-major: [x1,x2,...,xN,y1,y2,...,yN]
                for (size_t i = 0; i < nPoints; ++i) {
                    result.Points[frame].x[i] = pts_data[i];           // First column
                    result.Points[frame].y[i] = pts_data[i + nPoints]; // Second column
                }
            }
        }
    }
    
    // Read CorCoeffVec (cell array: each cell is Nx1 array)
    matvar_t* corr_var = Mat_VarGetStructFieldByName(dic2d_var, "CorCoeffVec", 0);
    if (corr_var && corr_var->class_type == MAT_C_CELL) {
        size_t nFrames = getCellArraySize(corr_var);
        result.CorCoeffVec.resize(nFrames);
        
        for (size_t frame = 0; frame < nFrames; ++frame) {
            matvar_t* frame_var = getCellElement(corr_var, frame);
            if (frame_var && frame_var->class_type == MAT_C_DOUBLE && frame_var->data) {
                result.CorCoeffVec[frame] = readDoubleArray(frame_var);
            }
        }
    }
    
    // Read ncorrInfo structure
    matvar_t* ncorr_var = Mat_VarGetStructFieldByName(dic2d_var, "ncorrInfo", 0);
    if (ncorr_var && ncorr_var->class_type == MAT_C_STRUCT) {
        // cutoff_corrcoef (array)
        matvar_t* cutoff_cc = Mat_VarGetStructFieldByName(ncorr_var, "cutoff_corrcoef", 0);
        if (cutoff_cc) {
            result.ncorrInfo.cutoff_corrcoef = readDoubleArray(cutoff_cc);
        }
        
        // cutoff_diffnorm (scalar)
        matvar_t* cutoff_dn = Mat_VarGetStructFieldByName(ncorr_var, "cutoff_diffnorm", 0);
        if (cutoff_dn) {
            result.ncorrInfo.cutoff_diffnorm = readScalar(cutoff_dn);
        }
        
        // cutoff_iteration (scalar)
        matvar_t* cutoff_it = Mat_VarGetStructFieldByName(ncorr_var, "cutoff_iteration", 0);
        if (cutoff_it) {
            result.ncorrInfo.cutoff_iteration = static_cast<int>(readScalar(cutoff_it));
        }
        
        // radius (scalar)
        matvar_t* radius_var = Mat_VarGetStructFieldByName(ncorr_var, "radius", 0);
        if (radius_var) {
            result.ncorrInfo.radius = static_cast<int>(readScalar(radius_var));
        }
        
        // spacing (scalar)
        matvar_t* spacing_var = Mat_VarGetStructFieldByName(ncorr_var, "spacing", 0);
        if (spacing_var) {
            result.ncorrInfo.spacing = static_cast<int>(readScalar(spacing_var));
        }
        
        // pixtounits (scalar)
        matvar_t* ptu_var = Mat_VarGetStructFieldByName(ncorr_var, "pixtounits", 0);
        if (ptu_var) {
            result.ncorrInfo.pixtounits = readScalar(ptu_var);
        }
        
        // lenscoef (scalar)
        matvar_t* lens_var = Mat_VarGetStructFieldByName(ncorr_var, "lenscoef", 0);
        if (lens_var) {
            result.ncorrInfo.lenscoef = static_cast<int>(readScalar(lens_var));
        }
        
        // subsettrunc (bool/int)
        matvar_t* subset_var = Mat_VarGetStructFieldByName(ncorr_var, "subsettrunc", 0);
        if (subset_var) {
            result.ncorrInfo.subsettrunc = static_cast<int>(readScalar(subset_var)) != 0;
        }
        
        // total_threads (scalar)
        matvar_t* threads_var = Mat_VarGetStructFieldByName(ncorr_var, "total_threads", 0);
        if (threads_var) {
            result.ncorrInfo.total_threads = static_cast<int>(readScalar(threads_var));
        }
        
        // type (string)
        matvar_t* type_var = Mat_VarGetStructFieldByName(ncorr_var, "type", 0);
        if (type_var) {
            result.ncorrInfo.type = readString(type_var);
        }
        
        // units (string)
        matvar_t* units_var = Mat_VarGetStructFieldByName(ncorr_var, "units", 0);
        if (units_var) {
            result.ncorrInfo.units = readString(units_var);
        }
        
        // stepanalysis struct
        matvar_t* step_var = Mat_VarGetStructFieldByName(ncorr_var, "stepanalysis", 0);
        if (step_var && step_var->class_type == MAT_C_STRUCT) {
            matvar_t* enabled_var = Mat_VarGetStructFieldByName(step_var, "enabled", 0);
            if (enabled_var) {
                result.ncorrInfo.stepanalysis.enabled = static_cast<int>(readScalar(enabled_var)) != 0;
            }
            
            matvar_t* step_type_var = Mat_VarGetStructFieldByName(step_var, "type", 0);
            if (step_type_var) {
                result.ncorrInfo.stepanalysis.type = readString(step_type_var);
            }
            
            matvar_t* auto_var = Mat_VarGetStructFieldByName(step_var, "auto", 0);
            if (auto_var) {
                result.ncorrInfo.stepanalysis.auto_update = static_cast<int>(readScalar(auto_var)) != 0;
            }
            
            matvar_t* step_step_var = Mat_VarGetStructFieldByName(step_var, "step", 0);
            if (step_step_var) {
                result.ncorrInfo.stepanalysis.step = static_cast<int>(readScalar(step_step_var));
            }
        }
    }
    
    std::cout << "Loaded DIC2DPairResults: " 
              << "nCamRef=" << result.nCamRef 
              << ", nCamDef=" << result.nCamDef
              << ", nImages=" << result.nImages
              << ", " << result.Points.size() << " frames"
              << ", " << result.Faces.size() / 3 << " faces" << std::endl;
    
    Mat_VarFree(dic2d_var);
    Mat_Close(matfp);
    return true;
}

} // namespace cppxdic
