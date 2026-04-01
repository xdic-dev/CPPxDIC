#pragma once

/**
 * MATLAB-equivalent functions for DIC processing.
 * 
 * These functions replicate specific MATLAB operations used in the xDIC pipeline,
 * prefixed with matlab_ to clearly indicate their origin and purpose.
 * 
 * - matlab_replacebadcorr: Equivalent of replacebadcorr() in step2_dic_finish.m
 * - matlab_shrink_mask: Equivalent of shrink_mask.m
 * - matlab_imgaussfilt3: Equivalent of imgaussfilt3() with 'spatial' FilterDomain
 */

#include "ncorr.h"
#include <opencv2/core.hpp>
#include <vector>

namespace cppxdic {

/**
 * Replaces badly correlated displacement data with spatiotemporally filtered values.
 * Equivalent to MATLAB replacebadcorr() in step2_dic_finish.m.
 *
 * Algorithm:
 *   1. Stack displacement fields into 3D volumes (H x W x Nframe)
 *   2. Identify bad correlation mask: corrcoef > level_corr_coef
 *   3. Apply 3D Gaussian filter (sigma_spatial, sigma_temporal) with replicate padding
 *   4. Shrink the valid mask by shrink_factor to avoid border artifacts
 *   5. Replace only bad-corr points within shrunk mask with filtered values
 *   6. Repeat filtering + replacement (double pass) for smoother results
 *
 * @param data         DIC analysis output (modified in-place)
 * @param level_corr_coef  Correlation coefficient threshold (MATLAB default: 1.0)
 * @param sigma_spatial    Spatial Gaussian sigma (MATLAB default: 1.0)
 * @param sigma_temporal   Temporal Gaussian sigma (MATLAB default: 2.0)
 * @param shrink_factor    Mask shrink factor to avoid border effects (MATLAB default: 0.9)
 * @return Number of replaced subsets across all frames
 */
int matlab_replacebadcorr(ncorr::DIC_analysis_output& data,
                          double level_corr_coef = 1.0,
                          double sigma_spatial = 1.0,
                          double sigma_temporal = 2.0,
                          double shrink_factor = 0.9);

/**
 * Applies a 3D Gaussian filter (spatial + temporal) to a volume of 2D fields.
 * Equivalent to MATLAB imgaussfilt3(volume, sigma, 'FilterDomain', 'spatial', 'padding', 'replicate').
 *
 * Uses separable filtering: 2D spatial Gaussian per frame, then 1D temporal Gaussian per pixel.
 *
 * @param volume          Input 3D data as vector of CV_64F Mats (H x W each)
 * @param sigma_spatial   Gaussian sigma for spatial (x,y) dimensions
 * @param sigma_temporal  Gaussian sigma for temporal (frame) dimension
 * @return Filtered volume (same dimensions)
 */
std::vector<cv::Mat> matlab_imgaussfilt3(const std::vector<cv::Mat>& volume,
                                         double sigma_spatial,
                                         double sigma_temporal);

/**
 * Shrinks a binary mask by a scale factor around its centroid.
 * Equivalent to MATLAB shrink_mask.m:
 *   - Find boundary of mask
 *   - Compute centroid
 *   - Scale polygon by factor around centroid
 *   - Fill scaled polygon
 *
 * @param mask    Input binary mask (CV_8U, nonzero = valid)
 * @param factor  Scale factor (0.9 = shrink to 90%)
 * @return Shrunk binary mask (CV_8U)
 */
cv::Mat matlab_shrink_mask(const cv::Mat& mask, double factor);

} // namespace cppxdic
