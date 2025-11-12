/**
 * ROI and Seed Point Manager for CPPXDIC
 * Implementation of ROI/seed loading and coordinate transformations
 * Based on MATLAB draw_ref_roi.m, draw_ref_seed.m, and map_*.m functions
 */

#include "roi_manager.h"
#include <matio.h>
#include <iostream>
#include <filesystem>
#include <cmath>

namespace cppxdic {

cv::Mat ROIManager::loadOrCreateROI(const BaseParameters& params,
                                   const cv::Mat& reference_image) {
    if (std::filesystem::exists(params.roifile)) {
        std::cout << "Loading ROI from: " << params.roifile << std::endl;
        return loadROIFromMat(params.roifile);
    } else {
        std::cout << "Creating full ROI (no ROI file found)" << std::endl;
        return createFullROI(reference_image.size());
    }
}

cv::Mat ROIManager::loadROIFromMat(const std::string& roi_file) {
    mat_t* matfp = Mat_Open(roi_file.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open ROI file: " << roi_file << std::endl;
        return cv::Mat();
    }
    
    // Read 'refmask' variable
    matvar_t* refmask_var = Mat_VarRead(matfp, "refmask");
    if (!refmask_var) {
        std::cerr << "Variable 'refmask' not found in " << roi_file << std::endl;
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
        
        // MATLAB is column-major, OpenCV is row-major
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                roi_mask.at<uint8_t>(y, x) = data[x * height + y];
            }
        }
    }
    
    Mat_VarFree(refmask_var);
    Mat_Close(matfp);
    
    return roi_mask;
}

SeedPoint ROIManager::loadSeedFromMat(const std::string& seed_file) {
    SeedPoint seed;
    
    mat_t* matfp = Mat_Open(seed_file.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open seed file: " << seed_file << std::endl;
        return seed;
    }
    
    // Read 'seed_point' variable (uint16 1D array [x, y])
    matvar_t* seed_var = Mat_VarRead(matfp, "seed_point");
    if (!seed_var) {
        std::cerr << "Variable 'seed_point' not found in " << seed_file << std::endl;
        Mat_Close(matfp);
        return seed;
    }
    
    if (seed_var->rank >= 1 && seed_var->nbytes >= 2 * sizeof(uint16_t)) {
        const uint16_t* data = static_cast<const uint16_t*>(seed_var->data);
        seed.pw = {static_cast<double>(data[0]), static_cast<double>(data[1])};
    }
    
    Mat_VarFree(seed_var);
    Mat_Close(matfp);
    
    return seed;
}

SeedPoint ROIManager::loadOrCreateSeed(const BaseParameters& params,
                                      const cv::Mat& roi_mask) {
    if (std::filesystem::exists(params.seedfile)) {
        std::cout << "Loading seed from: " << params.seedfile << std::endl;
        return loadSeedFromMat(params.seedfile);
    } else {
        std::cout << "Creating default seed at ROI center" << std::endl;
        SeedPoint seed;
        seed.pw = findROICenter(roi_mask);
        return seed;
    }
}

std::vector<double> ROIManager::mapPixel2Subset(const std::vector<double>& pixel_coords,
                                               int subset_spacing) {
    // Formula from map_pixel2subset.m: subset_coord = pixel_coord / (subset_spacing + 1)
    double divisor = static_cast<double>(subset_spacing + 1);
    return {pixel_coords[0] / divisor, pixel_coords[1] / divisor};
}

std::vector<double> ROIManager::mapSubset2Pixel(const std::vector<double>& subset_coords,
                                               int subset_spacing) {
    // Formula from map_subset2pixel.m: pixel_coord = subset_coord * (subset_spacing + 1)
    double multiplier = static_cast<double>(subset_spacing + 1);
    return {subset_coords[0] * multiplier, subset_coords[1] * multiplier};
}

std::vector<double> ROIManager::mapPointCoordinate(const std::vector<double>& point_sw,
                                                   const cv::Mat& U_mapped,
                                                   const cv::Mat& V_mapped) {
    // Convert subset coords to pixel indices (round to nearest)
    int x = static_cast<int>(std::round(point_sw[0]));
    int y = static_cast<int>(std::round(point_sw[1]));
    
    // Bounds checking
    if (x < 0 || x >= U_mapped.cols || y < 0 || y >= U_mapped.rows) {
        std::cerr << "Point coordinates out of bounds: (" << x << ", " << y << ")" << std::endl;
        return point_sw;
    }
    
    // Look up displacement at (y, x)
    double u = U_mapped.at<double>(y, x);
    double v = V_mapped.at<double>(y, x);
    
    // Add displacement to original point (in subset world coordinates)
    return {point_sw[0] + u, point_sw[1] + v};
}

bool ROIManager::loadMatchingResults(const std::string& matching_file,
                                    cv::Mat& refmask_REF,
                                    cv::Mat& refmask_trial,
                                    cv::Mat& U_mapped,
                                    cv::Mat& V_mapped,
                                    int subset_spacing) {
    mat_t* matfp = Mat_Open(matching_file.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open matching file: " << matching_file << std::endl;
        return false;
    }
    
    // Structure:
    // matching.reference_save.roi.mask → refmask_REF
    // matching.current_save.roi.mask → refmask_trial  
    // matching.data_dic_save.displacements.plot_u_ref_formatted → U
    // matching.data_dic_save.displacements.plot_v_ref_formatted → V
    
    // This is a simplified version - full implementation would need to navigate the struct hierarchy
    // For now, we'll load the basic structure
    
    // TODO: Complete implementation to match MATLAB structure
    // This requires proper struct navigation using matio
    
    Mat_Close(matfp);
    
    std::cout << "Warning: loadMatchingResults not fully implemented yet" << std::endl;
    return false;
}

cv::Mat ROIManager::createFullROI(const cv::Size& image_size) {
    // Create full ROI (all pixels valid)
    return cv::Mat::ones(image_size, CV_8UC1) * 255;
}

std::vector<double> ROIManager::findROICenter(const cv::Mat& roi_mask) {
    cv::Point2f center = computeROICenterOfMass(roi_mask);
    return {static_cast<double>(center.x), static_cast<double>(center.y)};
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
