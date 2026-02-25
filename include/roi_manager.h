/**
 * ROI and Seed Point Manager for CPPXDIC
 * Handles ROI loading/creation and seed point management
 * Equivalent to draw_ref_roi.m, draw_ref_seed.m, and related functions in Matlab xDIC
 */

#ifndef ROI_MANAGER_H
#define ROI_MANAGER_H

#include "parameters.h"
#include <opencv2/opencv.hpp>
#include <ncorr.h>
#include <string>
#include <vector>

namespace cppxdic {

/**
 * ROIManager class
 * Manages ROI masks and seed points for DIC analysis
 */
class ROIManager {
public:
    /**
     * Load or create ROI mask
     * If ROI file doesn't exist, creates a full-image ROI
     * Equivalent to checking and calling draw_ref_roi in Matlab
     * 
     * @param params Base parameters containing file paths
     * @param reference_image Reference image for ROI creation
     * @return ROI mask as cv::Mat (uint8, non-zero = inside ROI)
     */
    static cv::Mat loadOrCreateROI(const BaseParameters& params,
                                   const cv::Mat& reference_image);
    
    /**
     * Load ROI mask from MAT file
     * 
     * @param roi_file Path to ROI MAT file
     * @return ROI mask
     */
    static cv::Mat loadROIFromMat(const std::string& roi_file);
    
    /**
     * Load seed point from MAT file
     * 
     * @param seed_file Path to seed MAT file
     * @return Seed point coordinates [x, y] in pixel world
     */
    static SeedPoint loadSeedFromMat(const std::string& seed_file);
    
    /**
     * Load or create seed point
     * If seed file doesn't exist, creates a default seed point at ROI center
     * 
     * @param params Base parameters containing file paths
     * @param roi_mask ROI mask
     * @return Seed point structure
     */
    static SeedPoint loadOrCreateSeed(const BaseParameters& params,
                                     const cv::Mat& roi_mask);
    
    /**
     * Map pixel coordinates to subset coordinates
     * Equivalent to map_pixel2subset.m
     * 
     * @param pixel_coords Pixel world coordinates [x, y]
     * @param subset_spacing Subset spacing parameter
     * @return Subset world coordinates [x, y]
     */
    static std::vector<int> mapPixel2Subset(const std::vector<int>& pixel_coords,
                                               int subset_spacing);
    
    /**
     * Map subset coordinates to pixel coordinates
     * Equivalent to map_subset2pixel.m
     * 
     * @param subset_coords Subset world coordinates [x, y]
     * @param subset_spacing Subset spacing parameter
     * @return Pixel world coordinates [x, y]
     */
    static std::vector<int> mapSubset2Pixel(const std::vector<int>& subset_coords,
                                               int subset_spacing);
    
    /**
     * Map point coordinates using displacement fields
     * Equivalent to map_pointcoordinate.m
     * 
     * @param point_sw Point in subset world coordinates
     * @param U_mapped U displacement field (normalized by subset_spacing+1)
     * @param V_mapped V displacement field (normalized by subset_spacing+1)
     * @return Mapped point in subset world coordinates
     */
    static std::vector<int> mapPointCoordinate(const std::vector<int>& point_sw,
                                                  const cv::Mat& U_mapped,
                                                  const cv::Mat& V_mapped);
    
    /**
     * Load matching results from MAT file
     * Extracts ROI masks and displacement fields from matching file
     * 
     * @param matching_file Path to matching MAT file
     * @param refmask_REF Output: reference ROI mask
     * @param refmask_trial Output: trial ROI mask
     * @param U_mapped Output: U displacement field
     * @param V_mapped Output: V displacement field
     * @return Success status
     */
    static bool loadMatchingResults(const std::string& matching_file,
                                   cv::Mat& refmask_REF,
                                   cv::Mat& refmask_trial,
                                   cv::Mat& U_mapped,
                                   cv::Mat& V_mapped,
                                   int subset_spacing);
    
    /**
     * Create default ROI mask (full image)
     * 
     * @param image_size Size of the image
     * @return Full ROI mask (all 255)
     */
    static cv::Mat createFullROI(const cv::Size& image_size);
    
    /**
     * Find center of ROI mask
     * 
     * @param roi_mask ROI mask
     * @return Center coordinates [x, y]
     */
    static std::vector<int> findROICenter(const cv::Mat& roi_mask);
    
    /**
     * Convert ncorr ROI2D to OpenCV Mat
     * 
     * @param roi ncorr ROI2D object
     * @return OpenCV Mat representation
     */
    static cv::Mat ncorrROIToMat(const ncorr::ROI2D& roi);
    
    /**
     * Convert OpenCV Mat to ncorr ROI2D
     * 
     * @param mask OpenCV Mat mask
     * @return ncorr ROI2D object
     */
    static ncorr::ROI2D matToNcorrROI(const cv::Mat& mask);

private:
    /**
     * Compute ROI center of mass
     */
    static cv::Point2f computeROICenterOfMass(const cv::Mat& roi_mask);
};

} // namespace cppxdic

#endif // ROI_MANAGER_H
