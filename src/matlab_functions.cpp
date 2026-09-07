/**
 * MATLAB-equivalent functions for DIC processing.
 * See matlab_functions.h for documentation.
 */

#include "matlab_functions.h"
#include "logging.h"
#include <opencv2/imgproc.hpp>
#if CV_VERSION_MAJOR >= 5
#include <opencv2/geometry.hpp> // cv::moments / cv::contourArea moved out of imgproc in OpenCV 5
#endif
#include <iostream>
#include <cmath>
#include <algorithm>

namespace cppxdic {

// ---------------------------------------------------------------------------
// matlab_shrink_mask
// ---------------------------------------------------------------------------
cv::Mat matlab_shrink_mask(const cv::Mat& mask, double factor) {
    if (mask.empty() || factor >= 1.0) {
        return mask.clone();
    }

    // Find contours (equivalent to MATLAB bwboundaries on imgradient)
    cv::Mat binary;
    if (mask.type() != CV_8U) {
        mask.convertTo(binary, CV_8U);
    } else {
        binary = mask.clone();
    }
    cv::threshold(binary, binary, 0, 255, cv::THRESH_BINARY);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (contours.empty()) {
        return cv::Mat::zeros(mask.size(), CV_8U);
    }

    // Find the largest contour (main boundary)
    size_t largest_idx = 0;
    double largest_area = 0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double area = cv::contourArea(contours[i]);
        if (area > largest_area) {
            largest_area = area;
            largest_idx = i;
        }
    }

    // Compute centroid using moments (equivalent to MATLAB centroid(polyshape))
    cv::Moments m = cv::moments(contours[largest_idx]);
    if (m.m00 < 1e-10) {
        return cv::Mat::zeros(mask.size(), CV_8U);
    }
    double cx = m.m10 / m.m00;
    double cy = m.m01 / m.m00;

    // Scale contour around centroid (equivalent to MATLAB scale(polyshape, factor, [cx,cy]))
    std::vector<cv::Point> scaled_contour;
    scaled_contour.reserve(contours[largest_idx].size());
    for (const auto& pt : contours[largest_idx]) {
        double sx = cx + factor * (pt.x - cx);
        double sy = cy + factor * (pt.y - cy);
        scaled_contour.emplace_back(
            static_cast<int>(std::round(sx)),
            static_cast<int>(std::round(sy))
        );
    }

    // Fill the scaled polygon (equivalent to MATLAB poly2mask)
    cv::Mat result = cv::Mat::zeros(mask.size(), CV_8U);
    std::vector<std::vector<cv::Point>> scaled_contours = {scaled_contour};
    cv::fillPoly(result, scaled_contours, cv::Scalar(255));

    return result;
}

// ---------------------------------------------------------------------------
// matlab_imgaussfilt3
// ---------------------------------------------------------------------------
std::vector<cv::Mat> matlab_imgaussfilt3(const std::vector<cv::Mat>& volume,
                                         double sigma_spatial,
                                         double sigma_temporal) {
    if (volume.empty()) {
        return {};
    }

    const int N = static_cast<int>(volume.size());
    const int H = volume[0].rows;
    const int W = volume[0].cols;

    // Step 1: Spatial Gaussian filter on each frame
    // Kernel size from sigma: MATLAB uses ceil(sigma * 2) * 2 + 1
    int ksize_spatial = static_cast<int>(std::ceil(sigma_spatial * 2.0)) * 2 + 1;
    if (ksize_spatial < 3) ksize_spatial = 3;

    std::vector<cv::Mat> spatially_filtered(N);
    for (int f = 0; f < N; ++f) {
        cv::Mat frame;
        if (volume[f].type() != CV_64F) {
            volume[f].convertTo(frame, CV_64F);
        } else {
            frame = volume[f];
        }
        // BORDER_REPLICATE matches MATLAB 'padding','replicate'
        cv::GaussianBlur(frame, spatially_filtered[f],
                         cv::Size(ksize_spatial, ksize_spatial),
                         sigma_spatial, sigma_spatial,
                         cv::BORDER_REPLICATE);
    }

    if (N == 1 || sigma_temporal < 1e-10) {
        return spatially_filtered;
    }

    // Step 2: Temporal Gaussian filter per pixel
    // Build 1D Gaussian kernel for temporal dimension
    int ksize_temporal = static_cast<int>(std::ceil(sigma_temporal * 2.0)) * 2 + 1;
    if (ksize_temporal < 3) ksize_temporal = 3;
    int half_k = ksize_temporal / 2;

    std::vector<double> kernel(ksize_temporal);
    double kernel_sum = 0;
    for (int k = 0; k < ksize_temporal; ++k) {
        double t = static_cast<double>(k - half_k);
        kernel[k] = std::exp(-0.5 * t * t / (sigma_temporal * sigma_temporal));
        kernel_sum += kernel[k];
    }
    for (auto& v : kernel) {
        v /= kernel_sum;
    }

    // Apply temporal filter with replicate padding
    std::vector<cv::Mat> result(N);
    for (int f = 0; f < N; ++f) {
        result[f] = cv::Mat::zeros(H, W, CV_64F);
    }

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            for (int f = 0; f < N; ++f) {
                double val = 0;
                for (int k = 0; k < ksize_temporal; ++k) {
                    int src_f = f + k - half_k;
                    // Replicate padding: clamp to [0, N-1]
                    src_f = std::clamp(src_f, 0, N - 1);
                    val += spatially_filtered[src_f].at<double>(y, x) * kernel[k];
                }
                result[f].at<double>(y, x) = val;
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// matlab_replacebadcorr
// ---------------------------------------------------------------------------
int matlab_replacebadcorr(ncorr::DIC_analysis_output& data,
                          double level_corr_coef,
                          double sigma_spatial,
                          double sigma_temporal,
                          double shrink_factor) {
    if (data.disps.empty()) {
        return 0;
    }

    const int Nframe = static_cast<int>(data.disps.size());
    const int H = data.disps[0].data_height();
    const int W = data.disps[0].data_width();

    LOG_DEBUG << "  matlab_replacebadcorr: " << Nframe << " frames, "
              << H << "x" << W << " grid, cc_threshold=" << level_corr_coef;

    // Step 1: Stack displacement fields into 3D volumes (H x W x Nframe)
    std::vector<cv::Mat> u_vol(Nframe), v_vol(Nframe), cc_vol(Nframe);
    for (int f = 0; f < Nframe; ++f) {
        u_vol[f] = cv::Mat(H, W, CV_64F, 0.0);
        v_vol[f] = cv::Mat(H, W, CV_64F, 0.0);
        cc_vol[f] = cv::Mat(H, W, CV_64F, 0.0);

        const auto& u_arr = data.disps[f].get_u().get_array();
        const auto& v_arr = data.disps[f].get_v().get_array();
        const auto& cc_arr = data.disps[f].get_cc().get_array();

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                u_vol[f].at<double>(y, x) = u_arr(y, x);
                v_vol[f].at<double>(y, x) = v_arr(y, x);
                cc_vol[f].at<double>(y, x) = cc_arr(y, x);
            }
        }
    }

    // Step 2: Create bad correlation mask (3D: corrcoef > threshold)
    // MATLAB: mask_corr_coef = logical(corr_coef > level_corr_coef);
    std::vector<cv::Mat> mask_bad(Nframe);
    for (int f = 0; f < Nframe; ++f) {
        mask_bad[f] = cv::Mat(H, W, CV_8U, cv::Scalar(0));
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (cc_vol[f].at<double>(y, x) > level_corr_coef) {
                    mask_bad[f].at<uint8_t>(y, x) = 255;
                }
            }
        }
    }

    // Step 3: Compute shrunk mask to avoid border effects
    // MATLAB: mask_in = u(:,:,end)~=0;
    //         [mask_out] = shrink_mask(mask_in, 0.9);
    //         mask_out = repmat(mask_out, [1 1 Nframe]);
    cv::Mat mask_in(H, W, CV_8U, cv::Scalar(0));
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (u_vol[Nframe - 1].at<double>(y, x) != 0.0) {
                mask_in.at<uint8_t>(y, x) = 255;
            }
        }
    }
    cv::Mat mask_shrunk = matlab_shrink_mask(mask_in, shrink_factor);

    // Step 4: First pass - Gaussian filter and replace
    // MATLAB: uf = imgaussfilt3(u, sigma, 'FilterDomain','spatial','padding','replicate');
    std::vector<cv::Mat> uf = matlab_imgaussfilt3(u_vol, sigma_spatial, sigma_temporal);
    std::vector<cv::Mat> vf = matlab_imgaussfilt3(v_vol, sigma_spatial, sigma_temporal);

    // MATLAB: umodif1(mask_corr_coef & mask_out) = uf(mask_corr_coef & mask_out);
    std::vector<cv::Mat> u_modif1(Nframe), v_modif1(Nframe);
    int total_replaced = 0;
    for (int f = 0; f < Nframe; ++f) {
        u_modif1[f] = u_vol[f].clone();
        v_modif1[f] = v_vol[f].clone();
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (mask_bad[f].at<uint8_t>(y, x) && mask_shrunk.at<uint8_t>(y, x)) {
                    u_modif1[f].at<double>(y, x) = uf[f].at<double>(y, x);
                    v_modif1[f].at<double>(y, x) = vf[f].at<double>(y, x);
                    total_replaced++;
                }
            }
        }
    }

    // Step 5: Second pass - filter the already-modified data and replace again
    // MATLAB: uf = imgaussfilt3(umodif1, sigma, ...);
    //         umodif2(mask_corr_coef & mask_out) = uf(mask_corr_coef & mask_out);
    uf = matlab_imgaussfilt3(u_modif1, sigma_spatial, sigma_temporal);
    vf = matlab_imgaussfilt3(v_modif1, sigma_spatial, sigma_temporal);

    std::vector<cv::Mat> u_modif2(Nframe), v_modif2(Nframe);
    for (int f = 0; f < Nframe; ++f) {
        u_modif2[f] = u_vol[f].clone();
        v_modif2[f] = v_vol[f].clone();
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (mask_bad[f].at<uint8_t>(y, x) && mask_shrunk.at<uint8_t>(y, x)) {
                    u_modif2[f].at<double>(y, x) = uf[f].at<double>(y, x);
                    v_modif2[f].at<double>(y, x) = vf[f].at<double>(y, x);
                }
            }
        }
    }

    // Step 6: Reconstruct Disp2D objects with modified displacement data
    // Disp2D is immutable, so we must create new instances
    for (int f = 0; f < Nframe; ++f) {
        const auto& old_disp = data.disps[f];
        const auto& old_roi = old_disp.get_roi();
        auto scalefactor = old_disp.get_scalefactor();

        // Copy arrays from cv::Mat back to Array2D
        ncorr::Array2D<double> new_u(H, W);
        ncorr::Array2D<double> new_v(H, W);
        ncorr::Array2D<double> new_cc(H, W);

        const auto& old_cc = old_disp.get_cc().get_array();
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                new_u(y, x) = u_modif2[f].at<double>(y, x);
                new_v(y, x) = v_modif2[f].at<double>(y, x);
                new_cc(y, x) = old_cc(y, x);  // Preserve original correlation coefficients
            }
        }

        data.disps[f] = ncorr::Disp2D(
            std::move(new_v), std::move(new_u), std::move(new_cc),
            old_roi, scalefactor
        );
    }

    LOG_DEBUG << "  matlab_replacebadcorr: replaced " << total_replaced
              << " subsets across " << Nframe << " frames";

    return total_replaced;
}

} // namespace cppxdic
