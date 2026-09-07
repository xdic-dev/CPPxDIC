/**
 * ROI and Seed Point Manager for CPPXDIC
 * Implementation of ROI/seed loading and coordinate transformations
 * Based on MATLAB draw_ref_roi.m, draw_ref_seed.m, and map_*.m functions
 */

#include "roi_manager.h"
#include "mat_reader.h"
#include "logging.h"
#include <matio.h>
#if CV_VERSION_MAJOR >= 5
#include <opencv2/geometry.hpp> // cv::moments moved out of imgproc in OpenCV 5
#endif
#include <iostream>
#include <filesystem>
#include <cmath>

namespace cppxdic {

cv::Mat ROIManager::loadOrCreateROI(const BaseParameters& params,
                                   const cv::Mat& reference_image) {
    if (std::filesystem::exists(params.roifile)) {
        LOG_INFO << "Loading ROI from: " << params.roifile;
        return loadROIFromMat(params.roifile);
    } else {
        LOG_WARN << "ROI file not found, creating full ROI";
        return createFullROI(reference_image.size());
    }
}

cv::Mat ROIManager::loadROIFromMat(const std::string& roi_file) {
    mat_t* matfp = Mat_Open(roi_file.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        LOG_ERROR << "Failed to open ROI file: " << roi_file;
        return cv::Mat();
    }

    // Read 'refmask' variable
    matvar_t* refmask_var = Mat_VarRead(matfp, "refmask");
    if (!refmask_var) {
        LOG_ERROR << "Variable 'refmask' not found in " << roi_file;
        Mat_Close(matfp);
        return cv::Mat();
    }
    
    // Convert to cv::Mat
    cv::Mat roi_mask;
    if (refmask_var->class_type == MAT_C_UINT8 && refmask_var->rank == 2) {
        size_t height = refmask_var->dims[0];
        size_t width = refmask_var->dims[1];
        
        roi_mask = cv::Mat(height, width, CV_8UC1);
        const uint8_t* data = static_cast<const uint8_t*>(refmask_var->data);
        
        // Check if this is a MATLAB logical array (values are 0 and 1)
        bool is_logical = (refmask_var->isLogical != 0);
        
        // MATLAB is column-major, OpenCV is row-major
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                uint8_t value = data[x * height + y];
                // Scale logical values (0,1) to image values (0,255) for proper visualization
                roi_mask.at<uint8_t>(y, x) = is_logical ? (value * 255) : value;
            }
        }
        
        if (is_logical) {
            LOG_DEBUG << "  Loaded logical mask (scaled 0/1 to 0/255)";
        }
    } else {
        LOG_WARN << "Unexpected refmask type (class=" << refmask_var->class_type
                 << ", rank=" << refmask_var->rank << ")";
    }
    
    Mat_VarFree(refmask_var);
    Mat_Close(matfp);
    
    return roi_mask;
}

SeedPoint ROIManager::loadSeedFromMat(const std::string& seed_file) {
    SeedPoint seed;
    
    mat_t* matfp = Mat_Open(seed_file.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        LOG_ERROR << "Failed to open seed file: " << seed_file;
        return seed;
    }

    // Read 'seed_point' variable (can be uint16 or double array [x, y])
    matvar_t* seed_var = Mat_VarRead(matfp, "seed_point");
    if (!seed_var) {
        LOG_ERROR << "Variable 'seed_point' not found in " << seed_file;
        Mat_Close(matfp);
        return seed;
    }
    
    // Handle different data types
    if (seed_var->rank >= 1 && seed_var->data) {
        size_t num_elements = 1;
        for (int i = 0; i < seed_var->rank; i++) {
            num_elements *= seed_var->dims[i];
        }
        
        if (num_elements >= 2) {
            // Try different data types
            if (seed_var->data_type == MAT_T_UINT16 || seed_var->data_type == MAT_T_INT16) {
                const uint16_t* data = static_cast<const uint16_t*>(seed_var->data);
                seed.pw = {static_cast<int>(data[0]), static_cast<int>(data[1])};
            } else if (seed_var->data_type == MAT_T_DOUBLE) {
                const double* data = static_cast<const double*>(seed_var->data);
                seed.pw = {static_cast<int>(data[0]), static_cast<int>(data[1])};
            } else if (seed_var->data_type == MAT_T_SINGLE) {
                const float* data = static_cast<const float*>(seed_var->data);
                seed.pw = {static_cast<int>(data[0]), static_cast<int>(data[1])};
            } else {
                LOG_WARN << "Unexpected seed_point data type: " << seed_var->data_type;
                // Try to read as uint16 anyway
                const uint16_t* data = static_cast<const uint16_t*>(seed_var->data);
                seed.pw = {static_cast<int>(data[0]), static_cast<int>(data[1])};
            }
            LOG_DEBUG << "  Loaded seed point: (" << seed.pw[0] << ", " << seed.pw[1] << ")";
        }
    }
    
    Mat_VarFree(seed_var);
    Mat_Close(matfp);
    
    return seed;
}

SeedPoint ROIManager::loadOrCreateSeed(const BaseParameters& params,
                                      const cv::Mat& roi_mask) {
    if (std::filesystem::exists(params.seedfile)) {
        LOG_INFO << "Loading seed from: " << params.seedfile;
        return loadSeedFromMat(params.seedfile);
    } else {
        LOG_WARN << "Seed file not found, creating default seed at ROI center";
        SeedPoint seed;
        seed.pw = findROICenter(roi_mask);
        return seed;
    }
}

// Convert pixel coordinates to subset coordinates (reduced grid indices)
// MATLAB equivalent: round((pos-1)/(spacing+1)+1) for 1-indexed
// C++ equivalent: round(pos/(spacing+1)) for 0-indexed
// spacing+1 = scalefactor (the stride between subset points)
std::vector<int> ROIManager::mapPixel2Subset(const std::vector<int>& pixel_coords,
                                               int subset_spacing) {
    double scalefactor = static_cast<double>(subset_spacing + 1);
    return {
        static_cast<int>(std::round(pixel_coords[0] / scalefactor)),
        static_cast<int>(std::round(pixel_coords[1] / scalefactor))
    };
}

// Convert subset coordinates to pixel coordinates
// MATLAB equivalent: (pos-1)*(spacing+1)+1 for 1-indexed
// C++ equivalent: pos*(spacing+1) for 0-indexed
std::vector<int> ROIManager::mapSubset2Pixel(const std::vector<int>& subset_coords,
                                               int subset_spacing) {
    double scalefactor = static_cast<double>(subset_spacing + 1);
    return {
        static_cast<int>(std::round(subset_coords[0] * scalefactor)),
        static_cast<int>(std::round(subset_coords[1] * scalefactor))
    };
}

std::vector<int> ROIManager::mapPointCoordinate(const std::vector<int>& point_sw,
                                                   const cv::Mat& U_mapped,
                                                   const cv::Mat& V_mapped) {
    // Convert subset coords to pixel indices (round to nearest)
    int x = static_cast<int>(std::round(point_sw[0]));
    int y = static_cast<int>(std::round(point_sw[1]));

    LOG_DEBUG << "x: " << x << ", y: " << y;

    // Bounds checking
    if (x < 0 || x >= U_mapped.cols || y < 0 || y >= U_mapped.rows) {
        LOG_ERROR << "Point coordinates out of bounds: (" << x << ", " << y << ")";
        return point_sw;
    }
    
    // Look up displacement at (y, x)
    double u = U_mapped.at<double>(y, x);
    double v = V_mapped.at<double>(y, x);
    
    LOG_DEBUG << "u: " << u << ", v: " << v;

    // Add displacement to original point (in subset world coordinates)
    // point_sw is in subset coordinates, u/v are displacements in reduced grid units
    return {
        static_cast<int>(std::round(point_sw[0] + u)),
        static_cast<int>(std::round(point_sw[1] + v))
    };
}

bool ROIManager::loadMatchingResults(const std::string& matching_file,
                                    cv::Mat& refmask_REF,
                                    cv::Mat& refmask_trial,
                                    cv::Mat& U_mapped,
                                    cv::Mat& V_mapped,
                                    int subset_spacing) {
    mat_t* matfp = Mat_Open(matching_file.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        LOG_ERROR << "Failed to open matching file: " << matching_file;
        return false;
    }

    LOG_INFO << "Loading matching results from: " << matching_file;

    // Load reference mask: reference_save.roi.mask
    matvar_t* ref_roi_mask = MatReader::readNestedField(matfp, "reference_save.roi.mask");
    if (ref_roi_mask) {
        refmask_REF = MatReader::readImage(ref_roi_mask);
        Mat_VarFree(ref_roi_mask);
        LOG_DEBUG << "  Loaded reference ROI mask: " << refmask_REF.size();
    } else {
        LOG_WARN << "  Could not load reference_save.roi.mask";
    }

    // Load current mask: current_save.roi.mask
    matvar_t* cur_roi_mask = MatReader::readNestedField(matfp, "current_save.roi.mask");
    if (cur_roi_mask) {
        refmask_trial = MatReader::readImage(cur_roi_mask);
        Mat_VarFree(cur_roi_mask);
        LOG_DEBUG << "  Loaded current ROI mask: " << refmask_trial.size();
    } else {
        LOG_WARN << "  Could not load current_save.roi.mask";
    }

    // Load displacements: data_dic_save.displacements.plot_u_ref_formatted (cell array)
    matvar_t* u_cell = MatReader::readNestedField(matfp, "data_dic_save.displacements.plot_u_ref_formatted");
    if (u_cell && u_cell->class_type == MAT_C_CELL) {
        // Get first cell (frame 0)
        matvar_t* u_mat = MatReader::getCellElement(u_cell, 0);
        if (u_mat) {
            U_mapped = MatReader::readImage(u_mat);
            LOG_DEBUG << "  Loaded U displacements: " << U_mapped.size();
        }
        Mat_VarFree(u_cell);
    } else {
        LOG_WARN << "  Could not load plot_u_ref_formatted";
    }

    // Load V displacements: data_dic_save.displacements.plot_v_ref_formatted (cell array)
    matvar_t* v_cell = MatReader::readNestedField(matfp, "data_dic_save.displacements.plot_v_ref_formatted");
    if (v_cell && v_cell->class_type == MAT_C_CELL) {
        // Get first cell (frame 0)
        matvar_t* v_mat = MatReader::getCellElement(v_cell, 0);
        if (v_mat) {
            V_mapped = MatReader::readImage(v_mat);
            LOG_DEBUG << "  Loaded V displacements: " << V_mapped.size();
        }
        Mat_VarFree(v_cell);
    } else {
        LOG_WARN << "  Could not load plot_v_ref_formatted";
    }

    Mat_Close(matfp);

    bool success = !refmask_REF.empty() && !refmask_trial.empty();
    if (success) {
        LOG_INFO << "Successfully loaded matching results";
    }
    return success;
}

cv::Mat ROIManager::createFullROI(const cv::Size& image_size) {
    // Create full ROI (all pixels valid)
    LOG_INFO << "Creating full ROI (all pixels valid)";
    return cv::Mat::ones(image_size, CV_8UC1) * 255;
}

std::vector<int> ROIManager::findROICenter(const cv::Mat& roi_mask) {
    cv::Point2f center = computeROICenterOfMass(roi_mask);
    return {static_cast<int>(center.x), static_cast<int>(center.y)};
}

cv::Mat ROIManager::ncorrROIToMat(const ncorr::ROI2D& roi) {
    const auto& mask = roi.get_mask();
    size_t height = mask.height();
    size_t width = mask.width();
    
    cv::Mat cv_mask(height, width, CV_8UC1);
    
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            cv_mask.at<uint8_t>(y, x) = mask(y, x) ? 255 : 0;
        }
    }
    
    return cv_mask;
}

ncorr::ROI2D ROIManager::matToNcorrROI(const cv::Mat& mask) {
    // Convert cv::Mat to ncorr::Array2D<bool>
    size_t height = mask.rows;
    size_t width = mask.cols;
    
    ncorr::Array2D<bool> bool_mask(height, width);
    
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            bool_mask(y, x) = (mask.at<uint8_t>(y, x) > 0);
        }
    }
    
    return ncorr::ROI2D(bool_mask);
}

cv::Point2f ROIManager::computeROICenterOfMass(const cv::Mat& roi_mask) {
    cv::Moments m = cv::moments(roi_mask, true);
    
    if (m.m00 == 0) {
        // Fallback to image center if no valid pixels
        return cv::Point2f(roi_mask.cols / 2.0f, roi_mask.rows / 2.0f);
    }
    
    return cv::Point2f(m.m10 / m.m00, m.m01 / m.m00);
}

} // namespace cppxdic
