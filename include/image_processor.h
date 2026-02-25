/**
 * Image processing utilities for CPPXDIC
 * Handles image saturation and filtering operations
 * Equivalent to satur.m and filter_like_ben.m in Matlab xDIC
 */

#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

namespace cppxdic {

/**
 * ImageProcessor class
 * Provides image preprocessing operations for DIC analysis
 */
class ImageProcessor {
public:
    /**
     * Saturate image intensities
     * Equivalent to satur.m in Matlab
     * 
     * @param input Input images (vector of cv::Mat)
     * @param level Grayscale limit for saturation
     * @return Saturated images
     */
    static std::vector<cv::Mat> saturate(const std::vector<cv::Mat>& input, int level);
    
    /**
     * Saturate single image
     * 
     * @param input Input image
     * @param level Grayscale limit for saturation
     * @param method Saturation method ('high' or 'low')
     * @return Saturated image
     */
    static cv::Mat saturate(const cv::Mat& input, int level, const std::string& method = "high");
    
    /**
     * Filter images like Ben's method
     * Equivalent to filter_like_ben.m in Matlab
     * 
     * @param input Input images (saturated)
     * @param mask ROI mask
     * @param param_filt Filtering parameters [low, high]
     * @param gs_boundaries Optional pre-computed grayscale boundaries
     * @return Filtered images and grayscale boundaries
     */
    static std::pair<std::vector<cv::Mat>, std::pair<double, double>> 
        filterLikeBen(const std::vector<cv::Mat>& input, 
                      const cv::Mat& mask,
                      const std::vector<int>& param_filt,
                      const std::pair<double, double>* gs_boundaries = nullptr);
    
    /**
     * Apply bandpass filter to image using FFT
     * 
     * @param input Input image
     * @param param_filt Filtering parameters [low_freq, high_freq]
     * @return Filtered image (float)
     */
    static cv::Mat applyBandpassFilter(const cv::Mat& input,
                                       const std::vector<int>& param_filt);
    
    /**
     * Compute grayscale boundaries from masked region using min/max
     * 
     * @param image Input image
     * @param mask ROI mask
     * @return Grayscale boundaries [min, max]
     */
    static std::pair<double, double> computeGrayscaleBoundaries(const cv::Mat& image,
                                                                const cv::Mat& mask);
    
    /**
     * Compute grayscale boundaries using percentiles
     * 
     * @param image Input image (CV_32F)
     * @param mask ROI mask
     * @param lower_percentile Lower percentile (e.g., 5.0 for 5th percentile)
     * @param upper_percentile Upper percentile (e.g., 95.0 for 95th percentile)
     * @return Grayscale boundaries [lower, upper]
     */
    static std::pair<double, double> computePercentileBoundaries(const cv::Mat& image,
                                                                 const cv::Mat& mask,
                                                                 double lower_percentile,
                                                                 double upper_percentile);
    
    /**
     * Normalize and clamp image to [0, 255] using boundaries
     * 
     * @param image Input image (CV_32F)
     * @param boundaries Grayscale boundaries [min, max]
     * @return Normalized and clamped image (CV_8UC1)
     */
    static cv::Mat normalizeAndClamp(const cv::Mat& image,
                                    const std::pair<double, double>& boundaries);
    
    /**
     * Load images from file paths
     * 
     * @param paths Vector of image file paths
     * @param grayscale Load as grayscale if true
     * @return Vector of loaded images
     */
    static std::vector<cv::Mat> loadImages(const std::vector<std::string>& paths,
                                          bool grayscale = true);
    
    /**
     * Save images to disk
     * 
     * @param images Vector of images to save
     * @param base_path Base directory path
     * @param prefix Filename prefix
     * @return Success status
     */
    static bool saveImages(const std::vector<cv::Mat>& images,
                          const std::string& base_path,
                          const std::string& prefix);

    /**
     * Blur the outside of a mask
     * 
     * @param img Input image
     * @param mask ROI mask
     * @param kwidth Kernel width for Gaussian blur
     * @param kheight Kernel height for Gaussian blur
     * @return Blurred image
     */
    static cv::Mat blurOutsideMask(const cv::Mat& img, const cv::Mat& mask, int kwidth = 25, int kheight = 25);

private:
    /**
     * Apply histogram equalization with clipping
     * 
     * @param image Input image
     * @param clip_limit Clipping limit for histogram
     * @return Equalized image
     */
    static cv::Mat applyHistogramEqualization(const cv::Mat& image, double clip_limit = 2.0);
    
    /**
     * Normalize image to specified range
     * 
     * @param image Input image
     * @param min_val Minimum value
     * @param max_val Maximum value
     * @return Normalized image
     */
    static cv::Mat normalizeImage(const cv::Mat& image, double min_val, double max_val);
};

} // namespace cppxdic

#endif // IMAGE_PROCESSOR_H
