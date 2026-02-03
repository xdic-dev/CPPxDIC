/**
 * Validation Test for Ben's Image Filter
 * Compares C++ implementation against MATLAB reference data
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <matio.h>
#include <filesystem>
#include <opencv2/opencv.hpp>
#include "../../src/image_processor.cpp"

namespace fs = std::filesystem;

class FilterValidator {
public:
    struct TestResult {
        bool passed = true;
        std::string test_name;
        std::vector<std::string> errors;
        int num_pixels_compared = 0;
        int num_failures = 0;
        double max_error = 0.0;
        double mean_error = 0.0;
        double rmse = 0.0;
    };

    /**
     * Load MATLAB reference data for filter test
     */
    bool loadMatlabReference(const std::string& mat_file) {
        mat_t* matfp = Mat_Open(mat_file.c_str(), MAT_ACC_RDONLY);
        if (!matfp) {
            std::cerr << "Failed to open MATLAB reference file: " << mat_file << std::endl;
            return false;
        }

        // Load input images
        matvar_t* input_var = Mat_VarRead(matfp, "input_images");
        if (input_var) {
            loadImageSequence(input_var, input_images_matlab);
            Mat_VarFree(input_var);
        }

        // Load mask
        matvar_t* mask_var = Mat_VarRead(matfp, "mask");
        if (mask_var) {
            mask_matlab = loadImage(mask_var);
            Mat_VarFree(mask_var);
        }

        // Load filter parameters
        matvar_t* params_var = Mat_VarRead(matfp, "param_filt");
        if (params_var && params_var->class_type == MAT_C_DOUBLE) {
            double* data = static_cast<double*>(params_var->data);
            param_filt_matlab.clear();
            for (size_t i = 0; i < params_var->dims[0]; ++i) {
                param_filt_matlab.push_back(static_cast<int>(data[i]));
            }
            Mat_VarFree(params_var);
        }

        // Load expected filtered output
        matvar_t* output_var = Mat_VarRead(matfp, "filtered_images");
        if (output_var) {
            loadImageSequence(output_var, filtered_images_matlab);
            Mat_VarFree(output_var);
        }

        // Load grayscale boundaries
        matvar_t* gs_bounds_var = Mat_VarRead(matfp, "gs_boundaries");
        if (gs_bounds_var && gs_bounds_var->class_type == MAT_C_DOUBLE) {
            double* data = static_cast<double*>(gs_bounds_var->data);
            gs_boundaries_matlab.first = data[0];
            gs_boundaries_matlab.second = data[1];
            Mat_VarFree(gs_bounds_var);
        }

        Mat_Close(matfp);
        return true;
    }

    /**
     * Run C++ filter and compare against MATLAB
     */
    TestResult validateFilter() {
        TestResult result;
        result.test_name = "Ben's Image Filter";

        // Run C++ implementation
        cppxdic::ImageProcessor processor;
        auto [filtered_cpp, gs_bounds_cpp] = processor.filterLikeBen(
            input_images_matlab, mask_matlab, param_filt_matlab, nullptr
        );

        // Compare grayscale boundaries
        double bounds_error = std::abs(gs_bounds_cpp.first - gs_boundaries_matlab.first) +
                             std::abs(gs_bounds_cpp.second - gs_boundaries_matlab.second);
        if (bounds_error > 1e-6) {
            std::stringstream ss;
            ss << "GS boundaries mismatch: C++=[" << gs_bounds_cpp.first << ", " 
               << gs_bounds_cpp.second << "], MATLAB=[" << gs_boundaries_matlab.first 
               << ", " << gs_boundaries_matlab.second << "]";
            result.warnings.push_back(ss.str());
        }

        // Compare filtered images
        if (filtered_cpp.size() != filtered_images_matlab.size()) {
            result.errors.push_back("Number of filtered images mismatch");
            result.passed = false;
            return result;
        }

        for (size_t i = 0; i < filtered_cpp.size(); ++i) {
            compareImages(filtered_cpp[i], filtered_images_matlab[i], 
                         "Frame " + std::to_string(i), result);
        }

        // Calculate statistics
        if (result.num_pixels_compared > 0) {
            result.mean_error /= result.num_pixels_compared;
            result.rmse = std::sqrt(result.rmse / result.num_pixels_compared);
        }

        return result;
    }

    /**
     * Test with second camera using same boundaries
     */
    TestResult validateFilterSecondCamera() {
        TestResult result;
        result.test_name = "Ben's Filter - Second Camera";

        if (second_cam_images_matlab.empty()) {
            result.errors.push_back("No second camera data loaded");
            return result;
        }

        // Run with fixed boundaries from first camera
        cppxdic::ImageProcessor processor;
        auto [filtered_cpp, _] = processor.filterLikeBen(
            second_cam_images_matlab, mask_matlab, param_filt_matlab, &gs_boundaries_matlab
        );

        // Compare filtered images
        for (size_t i = 0; i < filtered_cpp.size() && i < filtered_second_cam_matlab.size(); ++i) {
            compareImages(filtered_cpp[i], filtered_second_cam_matlab[i],
                         "Cam2 Frame " + std::to_string(i), result);
        }

        if (result.num_pixels_compared > 0) {
            result.mean_error /= result.num_pixels_compared;
            result.rmse = std::sqrt(result.rmse / result.num_pixels_compared);
        }

        return result;
    }

    /**
     * Generate validation report
     */
    void generateReport(const std::vector<TestResult>& results) {
        std::cout << "\n========================================\n";
        std::cout << "IMAGE FILTER VALIDATION REPORT\n";
        std::cout << "========================================\n\n";

        for (const auto& result : results) {
            std::cout << "Test: " << result.test_name << "\n";
            std::cout << "  Status: " << (result.passed ? "PASSED" : "FAILED") << "\n";
            std::cout << "  Pixels compared: " << result.num_pixels_compared << "\n";
            std::cout << "  Pixel failures: " << result.num_failures << "\n";
            std::cout << "  Max error: " << result.max_error << "\n";
            std::cout << "  Mean error: " << result.mean_error << "\n";
            std::cout << "  RMSE: " << result.rmse << "\n";
            std::cout << "  Pass rate: " << std::fixed << std::setprecision(2)
                      << (100.0 * (result.num_pixels_compared - result.num_failures) / 
                          std::max(1, result.num_pixels_compared)) << "%\n";

            if (!result.errors.empty()) {
                std::cout << "  Errors:\n";
                for (const auto& error : result.errors) {
                    std::cout << "    - " << error << "\n";
                }
            }

            if (!result.warnings.empty()) {
                std::cout << "  Warnings:\n";
                for (const auto& warning : result.warnings) {
                    std::cout << "    - " << warning << "\n";
                }
            }
            std::cout << "\n";
        }
    }

private:
    // MATLAB reference data
    std::vector<cv::Mat> input_images_matlab;
    std::vector<cv::Mat> filtered_images_matlab;
    std::vector<cv::Mat> second_cam_images_matlab;
    std::vector<cv::Mat> filtered_second_cam_matlab;
    cv::Mat mask_matlab;
    std::vector<int> param_filt_matlab;
    std::pair<double, double> gs_boundaries_matlab;

    cv::Mat loadImage(matvar_t* var) {
        if (!var || var->class_type != MAT_C_DOUBLE) return cv::Mat();

        double* data = static_cast<double*>(var->data);
        int rows = var->dims[0];
        int cols = var->dims[1];
        
        cv::Mat img(rows, cols, CV_64F);
        
        // MATLAB stores in column-major, OpenCV uses row-major
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                img.at<double>(r, c) = data[c * rows + r];
            }
        }
        
        return img;
    }

    void loadImageSequence(matvar_t* var, std::vector<cv::Mat>& images) {
        images.clear();
        
        if (var->class_type == MAT_C_CELL) {
            // Cell array of images
            size_t n_images = var->dims[0] * var->dims[1];
            for (size_t i = 0; i < n_images; ++i) {
                matvar_t* img_var = Mat_VarGetCell(var, i);
                if (img_var) {
                    images.push_back(loadImage(img_var));
                }
            }
        } else if (var->rank == 3) {
            // 3D array (rows x cols x n_frames)
            double* data = static_cast<double*>(var->data);
            int rows = var->dims[0];
            int cols = var->dims[1];
            int n_frames = var->dims[2];
            
            for (int f = 0; f < n_frames; ++f) {
                cv::Mat img(rows, cols, CV_64F);
                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        img.at<double>(r, c) = data[f * rows * cols + c * rows + r];
                    }
                }
                images.push_back(img);
            }
        }
    }

    void compareImages(const cv::Mat& cpp_img, const cv::Mat& matlab_img,
                      const std::string& label, TestResult& result) {
        if (cpp_img.size() != matlab_img.size()) {
            result.errors.push_back(label + ": Size mismatch");
            result.passed = false;
            return;
        }

        // Convert both to same type for comparison
        cv::Mat cpp_64f, matlab_64f;
        cpp_img.convertTo(cpp_64f, CV_64F);
        matlab_img.convertTo(matlab_64f, CV_64F);

        double sum_squared_error = 0.0;
        int failed_pixels = 0;
        double tolerance = 1.0; // Allow 1 gray level difference

        for (int r = 0; r < cpp_64f.rows; ++r) {
            for (int c = 0; c < cpp_64f.cols; ++c) {
                double cpp_val = cpp_64f.at<double>(r, c);
                double matlab_val = matlab_64f.at<double>(r, c);
                double error = std::abs(cpp_val - matlab_val);
                
                result.num_pixels_compared++;
                result.max_error = std::max(result.max_error, error);
                result.mean_error += error;
                sum_squared_error += error * error;
                
                if (error > tolerance) {
                    failed_pixels++;
                    result.num_failures++;
                    
                    // Log first few failures
                    if (result.errors.size() < 5) {
                        std::stringstream ss;
                        ss << label << "[" << r << "," << c << "]: "
                           << "C++=" << cpp_val << ", MATLAB=" << matlab_val
                           << ", error=" << error;
                        result.errors.push_back(ss.str());
                    }
                }
            }
        }
        
        result.rmse += sum_squared_error;
        
        if (failed_pixels > cpp_64f.total() * 0.01) { // More than 1% pixels failed
            result.passed = false;
        }
    }
};

int main(int argc, char** argv) {
    std::string matlab_ref_file = "test_data/filter_reference.mat";
    
    if (argc > 1) {
        matlab_ref_file = argv[1];
    }

    std::cout << "=== Ben's Image Filter Validation Test ===\n";
    std::cout << "Reference file: " << matlab_ref_file << "\n\n";

    if (!fs::exists(matlab_ref_file)) {
        std::cerr << "Reference file not found. Please provide MATLAB reference data.\n";
        std::cerr << "Expected format: mat file with variables:\n";
        std::cerr << "  - input_images: cell array or 3D array of input images\n";
        std::cerr << "  - mask: 2D binary mask\n";
        std::cerr << "  - param_filt: [2 x 1] filter parameters\n";
        std::cerr << "  - filtered_images: cell array or 3D array of expected output\n";
        std::cerr << "  - gs_boundaries: [2 x 1] grayscale boundaries\n";
        return 1;
    }

    FilterValidator validator;
    
    // Load MATLAB reference data
    if (!validator.loadMatlabReference(matlab_ref_file)) {
        std::cerr << "Failed to load MATLAB reference data\n";
        return 1;
    }

    std::vector<FilterValidator::TestResult> results;

    // Test first camera filtering
    results.push_back(validator.validateFilter());

    // Test second camera with fixed boundaries
    results.push_back(validator.validateFilterSecondCamera());

    // Generate report
    validator.generateReport(results);

    bool all_passed = true;
    for (const auto& result : results) {
        all_passed = all_passed && result.passed;
    }

    return all_passed ? 0 : 1;
}
