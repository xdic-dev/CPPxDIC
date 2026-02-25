/**
 * Image processing utilities for CPPXDIC
 * Implementation of saturation and filtering operations
 * Based on MATLAB satur.m and filter_like_ben.m
 */

#include "image_processor.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <opencv2/imgcodecs.hpp>
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

cv::Mat ImageProcessor::saturate(const cv::Mat& input, int level, const std::string& method) {
    // Convert to grayscale if needed
    cv::Mat gray;
    if (input.channels() == 3) {
        std::cout << "Warning - Converting to grayscale" << std::endl;
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }
    
    // Ensure uint8 format
    if (gray.type() != CV_8UC1) {
        std::cout << "Warning - Converting to uint8" << std::endl;
        gray.convertTo(gray, CV_8UC1);
    }
    
    cv::Mat output = gray.clone();

    if (method == "high") {
        cv::threshold(output, output, level, level, cv::THRESH_TRUNC);
    } else if (method == "low") {
        output.setTo(level, output < level);
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
    // Convert to float for processing (like MATLAB's double)
    cv::Mat float_img;
    input.convertTo(float_img, CV_64F);
    
    // Apply Gaussian bandpass filter using FFT
    // param_filt = [r1, r2] where r1 is high-pass threshold, r2 is low-pass threshold
    // MATLAB: filter = exp(-r^2/(2*r2^2)) * (1 - exp(-r^2/(2*r1^2)))
    double r1 = static_cast<double>(param_filt[0]);
    double r2 = static_cast<double>(param_filt[1]);
    
    int rows = float_img.rows;
    int cols = float_img.cols;
    
    // Create Gaussian bandpass filter (matching MATLAB bandpassfft.m)
    cv::Mat filter_mask(rows, cols, CV_64F);
    int cx = cols / 2;
    int cy = rows / 2;
    
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            double dx = x - cx;
            double dy = y - cy;
            double r_sq = dx * dx + dy * dy;
            
            // Gaussian bandpass: exp(-r^2/(2*r2^2)) * (1 - exp(-r^2/(2*r1^2)))
            double low_pass = std::exp(-r_sq / (2.0 * r2 * r2));
            double high_pass = (r1 != 0) ? (1.0 - std::exp(-r_sq / (2.0 * r1 * r1))) : 1.0;
            filter_mask.at<double>(y, x) = low_pass * high_pass;
        }
    }
    
    // Perform FFT (matching MATLAB: out = real(ifft2(fft2(in).*fftshift(filter))))
    cv::Mat planes[] = {float_img, cv::Mat::zeros(float_img.size(), CV_64F)};
    cv::Mat complex_img;
    cv::merge(planes, 2, complex_img);
    cv::dft(complex_img, complex_img);
    
    // fftshift the filter to match MATLAB's fftshift(filter)
    cv::Mat filter_shifted;
    int cx2 = filter_mask.cols / 2;
    int cy2 = filter_mask.rows / 2;
    cv::Mat q0(filter_mask, cv::Rect(0, 0, cx2, cy2));
    cv::Mat q1(filter_mask, cv::Rect(cx2, 0, cols - cx2, cy2));
    cv::Mat q2(filter_mask, cv::Rect(0, cy2, cx2, rows - cy2));
    cv::Mat q3(filter_mask, cv::Rect(cx2, cy2, cols - cx2, rows - cy2));
    
    cv::Mat tmp;
    filter_shifted = cv::Mat(rows, cols, CV_64F);
    q3.copyTo(filter_shifted(cv::Rect(0, 0, cols - cx2, rows - cy2)));
    q0.copyTo(filter_shifted(cv::Rect(cols - cx2, rows - cy2, cx2, cy2)));
    q1.copyTo(filter_shifted(cv::Rect(0, rows - cy2, cols - cx2, cy2)));
    q2.copyTo(filter_shifted(cv::Rect(cols - cx2, 0, cx2, rows - cy2)));
    
    // Apply filter in frequency domain
    cv::split(complex_img, planes);
    planes[0] = planes[0].mul(filter_shifted);
    planes[1] = planes[1].mul(filter_shifted);
    cv::merge(planes, 2, complex_img);
    
    // Inverse DFT
    cv::Mat filtered_img;
    cv::idft(complex_img, filtered_img, cv::DFT_SCALE);
    cv::split(filtered_img, planes);
    
    return planes[0];  // Return real part
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
    std::vector<double> values;
    
    // Convert to double if needed
    cv::Mat double_img;
    if (image.type() == CV_64F) {
        double_img = image;
    } else {
        image.convertTo(double_img, CV_64F);
    }
    
    for (int y = 0; y < double_img.rows; ++y) {
        for (int x = 0; x < double_img.cols; ++x) {
            if (mask.at<uint8_t>(y, x) > 0) {
                values.push_back(double_img.at<double>(y, x));
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
    // MATLAB: imbdp_norm = ((imbdp-y(1))/(y(2)-y(1)));
    //         imbdp_norm = satur(satur(imbdp_norm,'method','low','level',0),'method','high','level',1);
    
    cv::Mat double_img;
    if (image.type() == CV_64F) {
        double_img = image;
    } else {
        image.convertTo(double_img, CV_64F);
    }
    
    cv::Mat normalized;
    double range = boundaries.second - boundaries.first;
    if (range > 0) {
        normalized = (double_img - boundaries.first) / range;
        // Saturate (clamp) to [0, 1] - matching MATLAB's satur calls
        cv::threshold(normalized, normalized, 1.0, 1.0, cv::THRESH_TRUNC);
        cv::threshold(normalized, normalized, 0.0, 0.0, cv::THRESH_TOZERO);
    } else {
        normalized = cv::Mat::zeros(image.size(), CV_64F);
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


cv::Mat ImageProcessor::blurOutsideMask(const cv::Mat& img, const cv::Mat& mask, int kwidth, int kheight) {
    cv::Mat blurred, result;
    
    // Apply Gaussian blur to the entire image
    cv::GaussianBlur(img, blurred, cv::Size(kwidth, kheight), 15);

    // Create result image
    img.copyTo(result);

    // Define the region to blur: where mask is 0 (or not present)
    cv::Mat blurRegion = (mask == 0); // Inverted mask

    // Copy blurred pixels to the non-masked area
    blurred.copyTo(result, blurRegion);
    
    return result;
}       

} // namespace cppxdic
