/**
 * Image processing utilities for CPPXDIC
 * Implementation of saturation and filtering operations
 * Based on MATLAB satur.m and filter_like_ben.m
 */

#include "image_processor.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace cppxdic {

// Saturate images - equivalent to satur.m
std::vector<cv::Mat> ImageProcessor::saturate(const std::vector<cv::Mat>& input, int level) {
    std::vector<cv::Mat> output;
    output.reserve(input.size());
    
    for (const auto& img : input) {
        output.push_back(saturate(img, level));
    }
    
    return output;
}

cv::Mat ImageProcessor::saturate(const cv::Mat& input, int level) {
    // Convert to grayscale if needed
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }
    
    // Ensure uint8 format
    if (gray.type() != CV_8UC1) {
        gray.convertTo(gray, CV_8UC1);
    }
    
    cv::Mat output = gray.clone();
    
    // Saturate: clip values at level threshold
    // MATLAB satur.m: pixels >= level become 255
    for (int y = 0; y < output.rows; ++y) {
        for (int x = 0; x < output.cols; ++x) {
            uint8_t val = output.at<uint8_t>(y, x);
            if (val >= level) {
                output.at<uint8_t>(y, x) = 255;
            }
        }
    }
    
    // Stretch histogram to use full [0, 255] range
    double minVal, maxVal;
    cv::minMaxLoc(output, &minVal, &maxVal);
    
    if (maxVal > minVal) {
        output.convertTo(output, CV_8UC1, 255.0 / (maxVal - minVal), -minVal * 255.0 / (maxVal - minVal));
    }
    
    return output;
}

// Filter images like Ben's method - equivalent to filter_like_ben.m
std::pair<std::vector<cv::Mat>, std::pair<double, double>> 
ImageProcessor::filterLikeBen(const std::vector<cv::Mat>& input,
                              const cv::Mat& mask,
                              const std::vector<int>& param_filt,
                              const std::pair<double, double>* gs_boundaries) {
    
    std::vector<cv::Mat> filtered_images;
    std::pair<double, double> boundaries;
    
    // Apply bandpass filter to all images first
    for (const auto& img : input) {
        cv::Mat filtered = applyBandpassFilter(img, param_filt);
        filtered_images.push_back(filtered);
    }
    
    // Compute boundaries from FIRST FILTERED image using percentiles (5th, 95th) if not provided
    if (gs_boundaries == nullptr) {
        boundaries = computePercentileBoundaries(filtered_images[0], mask, 5.0, 95.0);
        std::cout << "  Computed filter boundaries: [" << boundaries.first << ", " << boundaries.second << "]" << std::endl;
    } else {
        boundaries = *gs_boundaries;
    }
    
    // Normalize each filtered image using the boundaries
    for (size_t i = 0; i < filtered_images.size(); ++i) {
        filtered_images[i] = normalizeAndClamp(filtered_images[i], boundaries);
    }
    
    return {filtered_images, boundaries};
}

cv::Mat ImageProcessor::applyBandpassFilter(const cv::Mat& input,
                                            const std::vector<int>& param_filt) {
    // Convert to float for processing
    cv::Mat float_img;
    input.convertTo(float_img, CV_32F);
    
    // Apply bandpass filter using FFT
    // param_filt = [low_freq, high_freq] in pixel units
    int low_freq = param_filt[0];
    int high_freq = param_filt[1];
    
    // Perform DFT
    cv::Mat padded;
    int m = cv::getOptimalDFTSize(float_img.rows);
    int n = cv::getOptimalDFTSize(float_img.cols);
    cv::copyMakeBorder(float_img, padded, 0, m - float_img.rows, 0, n - float_img.cols, 
                       cv::BORDER_CONSTANT, cv::Scalar::all(0));
    
    cv::Mat planes[] = {cv::Mat_<float>(padded), cv::Mat::zeros(padded.size(), CV_32F)};
    cv::Mat complex_img;
    cv::merge(planes, 2, complex_img);
    cv::dft(complex_img, complex_img);
    
    // Create bandpass filter mask
    cv::Mat filter_mask = cv::Mat::ones(complex_img.size(), CV_32F);
    int cx = filter_mask.cols / 2;
    int cy = filter_mask.rows / 2;
    
    for (int y = 0; y < filter_mask.rows; ++y) {
        for (int x = 0; x < filter_mask.cols; ++x) {
            double dx = x - cx;
            double dy = y - cy;
            double dist = std::sqrt(dx * dx + dy * dy);
            
            // Bandpass: pass frequencies between low and high
            if (dist < low_freq || dist > high_freq) {
                filter_mask.at<float>(y, x) = 0.0f;
            }
        }
    }
    
    // Apply filter in frequency domain
    cv::Mat filtered_complex;
    cv::split(complex_img, planes);
    planes[0] = planes[0].mul(filter_mask);
    planes[1] = planes[1].mul(filter_mask);
    cv::merge(planes, 2, filtered_complex);
    
    // Inverse DFT
    cv::Mat filtered_img;
    cv::idft(filtered_complex, filtered_img, cv::DFT_SCALE | cv::DFT_REAL_OUTPUT);
    filtered_img = filtered_img(cv::Rect(0, 0, input.cols, input.rows));
    
    return filtered_img;
}

std::pair<double, double> ImageProcessor::computeGrayscaleBoundaries(const cv::Mat& image,
                                                                     const cv::Mat& mask) {
    // Compute min/max grayscale values within masked region
    cv::Mat float_img;
    image.convertTo(float_img, CV_32F);
    
    double min_val = std::numeric_limits<double>::max();
    double max_val = std::numeric_limits<double>::lowest();
    
    for (int y = 0; y < image.rows; ++y) {
        for (int x = 0; x < image.cols; ++x) {
            if (mask.at<uint8_t>(y, x) > 0) {
                double val = float_img.at<float>(y, x);
                min_val = std::min(min_val, val);
                max_val = std::max(max_val, val);
            }
        }
    }
    
    return {min_val, max_val};
}

std::pair<double, double> ImageProcessor::computePercentileBoundaries(const cv::Mat& image,
                                                                      const cv::Mat& mask,
                                                                      double lower_percentile,
                                                                      double upper_percentile) {
    // Collect all pixel values within masked region
    std::vector<float> values;
    
    for (int y = 0; y < image.rows; ++y) {
        for (int x = 0; x < image.cols; ++x) {
            if (mask.at<uint8_t>(y, x) > 0) {
                values.push_back(image.at<float>(y, x));
            }
        }
    }
    
    if (values.empty()) {
        std::cerr << "Warning: No pixels in mask for percentile computation" << std::endl;
        return {0.0, 255.0};
    }
    
    // Sort values
    std::sort(values.begin(), values.end());
    
    // Compute percentile indices
    size_t lower_idx = static_cast<size_t>(values.size() * lower_percentile / 100.0);
    size_t upper_idx = static_cast<size_t>(values.size() * upper_percentile / 100.0);
    
    lower_idx = std::min(lower_idx, values.size() - 1);
    upper_idx = std::min(upper_idx, values.size() - 1);
    
    return {values[lower_idx], values[upper_idx]};
}

cv::Mat ImageProcessor::normalizeAndClamp(const cv::Mat& image,
                                         const std::pair<double, double>& boundaries) {
    // Normalize: ((image - min) / (max - min))
    // Then saturate to [0, 1] and scale to [0, 255]
    cv::Mat normalized;
    
    double range = boundaries.second - boundaries.first;
    if (range > 0) {
        normalized = (image - boundaries.first) / range;
        // Saturate (clamp) to [0, 1]
        cv::threshold(normalized, normalized, 1.0, 1.0, cv::THRESH_TRUNC);
        cv::threshold(normalized, normalized, 0.0, 0.0, cv::THRESH_TOZERO);
    } else {
        normalized = cv::Mat::zeros(image.size(), CV_32F);
    }
    
    // Convert to uint8 [0, 255]
    cv::Mat output;
    normalized.convertTo(output, CV_8UC1, 255.0);
    
    return output;
}

std::vector<cv::Mat> ImageProcessor::loadImages(const std::vector<std::string>& paths,
                                               bool grayscale) {
    std::vector<cv::Mat> images;
    images.reserve(paths.size());
    
    for (const auto& path : paths) {
        cv::Mat img = cv::imread(path, grayscale ? cv::IMREAD_GRAYSCALE : cv::IMREAD_COLOR);
        if (img.empty()) {
            std::cerr << "Failed to load image: " << path << std::endl;
            continue;
        }
        images.push_back(img);
    }
    
    return images;
}

bool ImageProcessor::saveImages(const std::vector<cv::Mat>& images,
                               const std::string& base_path,
                               const std::string& prefix) {
    for (size_t i = 0; i < images.size(); ++i) {
        std::ostringstream filename;
        filename << base_path << "/" << prefix << "_" << std::setw(6) << std::setfill('0') << i << ".png";
        
        if (!cv::imwrite(filename.str(), images[i])) {
            std::cerr << "Failed to save image: " << filename.str() << std::endl;
            return false;
        }
    }
    
    return true;
}

cv::Mat ImageProcessor::applyHistogramEqualization(const cv::Mat& image, double clip_limit) {
    cv::Mat output;
    
    // Apply CLAHE (Contrast Limited Adaptive Histogram Equalization)
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(clip_limit, cv::Size(8, 8));
    clahe->apply(image, output);
    
    return output;
}

cv::Mat ImageProcessor::normalizeImage(const cv::Mat& image, double min_val, double max_val) {
    cv::Mat output;
    
    if (max_val > min_val) {
        // Normalize to [0, 255] using boundaries
        image.convertTo(output, CV_32F, 255.0 / (max_val - min_val), -min_val * 255.0 / (max_val - min_val));
        
        // Clip to [0, 255] range
        cv::threshold(output, output, 255.0, 255.0, cv::THRESH_TRUNC);
        cv::threshold(output, output, 0.0, 0.0, cv::THRESH_TOZERO);
    } else {
        output = image.clone();
    }
    
    return output;
}

} // namespace cppxdic
