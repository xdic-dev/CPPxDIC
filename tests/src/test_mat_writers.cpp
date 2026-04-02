/**
 * Unit Tests for MAT File Writers
 * Tests all MAT file writing functions in mat_writer.cpp
 */

#include "mat_writer.h"
#include "dic_structures.h"
#include <opencv2/opencv.hpp>
#include <matio.h>
#include <iostream>
#include <filesystem>
#include <cassert>

using namespace cppxdic;

// Test output directory
const std::string TEST_OUTPUT_DIR = "test_output";

// Helper function to create test directory
void setupTestDirectory() {
    if (!std::filesystem::exists(TEST_OUTPUT_DIR)) {
        std::filesystem::create_directory(TEST_OUTPUT_DIR);
    }
}

// Helper function to check if file exists
bool fileExists(const std::string& filename) {
    return std::filesystem::exists(filename);
}

// Helper function to get file size
size_t getFileSize(const std::string& filename) {
    return std::filesystem::file_size(filename);
}

bool hasStructField(matvar_t* struct_var, const char* field_name) {
    return struct_var && struct_var->class_type == MAT_C_STRUCT &&
           Mat_VarGetStructFieldByName(struct_var, field_name, 0) != nullptr;
}

// ============================================================================
// Test 1: writeMatchingFile()
// ============================================================================
bool test_writeMatchingFile() {
    std::cout << "\n=== Test 1: writeMatchingFile() ===" << std::endl;
    
    try {
        // Create test images
        cv::Mat ref_img = cv::Mat::zeros(100, 100, CV_64F);
        cv::Mat cur_img = cv::Mat::ones(100, 100, CV_64F) * 0.5;
        
        // Create test ROIs
        cv::Mat ref_roi = cv::Mat::ones(100, 100, CV_8U);
        cv::Mat cur_roi = cv::Mat::ones(100, 100, CV_8U);
        
        // Create test DIC output (empty for this test)
        ncorr::DIC_analysis_output dic_output;
        
        // Create test dispinfo
        std::map<std::string, double> dispinfo;
        dispinfo["cutoff_corrcoef"] = 0.5;
        dispinfo["cutoff_diffnorm"] = 0.01;
        dispinfo["cutoff_iteration"] = 50;
        dispinfo["radius"] = 15;
        dispinfo["spacing"] = 5;
        dispinfo["subsettrunc"] = 0;
        dispinfo["total_threads"] = 8;
        
        // Write file
        std::string filename = TEST_OUTPUT_DIR + "/test_MATCHING.mat";
        bool success = MatWriter::writeMatchingFile(
            filename, ref_img, cur_img, ref_roi, cur_roi, dic_output, dispinfo
        );
        
        // Verify
        assert(success && "writeMatchingFile should succeed");
        assert(fileExists(filename) && "Output file should exist");
        assert(getFileSize(filename) > 0 && "Output file should not be empty");
        
        std::cout << "✓ writeMatchingFile test PASSED" << std::endl;
        std::cout << "  File: " << filename << " (" << getFileSize(filename) << " bytes)" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ writeMatchingFile test FAILED: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Test 2: writeROIMaskFile()
// ============================================================================
bool test_writeROIMaskFile() {
    std::cout << "\n=== Test 2: writeROIMaskFile() ===" << std::endl;
    
    try {
        // Create test mask
        cv::Mat mask = cv::Mat::zeros(200, 200, CV_8U);
        cv::circle(mask, cv::Point(100, 100), 50, cv::Scalar(255), -1);
        
        // Write file
        std::string filename = TEST_OUTPUT_DIR + "/test_REF_MASK.mat";
        bool success = MatWriter::writeROIMaskFile(filename, mask);
        
        // Verify
        assert(success && "writeROIMaskFile should succeed");
        assert(fileExists(filename) && "Output file should exist");
        assert(getFileSize(filename) > 0 && "Output file should not be empty");
        
        std::cout << "✓ writeROIMaskFile test PASSED" << std::endl;
        std::cout << "  File: " << filename << " (" << getFileSize(filename) << " bytes)" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ writeROIMaskFile test FAILED: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Test 3: writeSeedFile()
// ============================================================================
bool test_writeSeedFile() {
    std::cout << "\n=== Test 3: writeSeedFile() ===" << std::endl;
    
    try {
        // Test seed coordinates
        int seed_x = 100;
        int seed_y = 150;
        
        // Write file
        std::string filename = TEST_OUTPUT_DIR + "/test_REF_SEED.mat";
        bool success = MatWriter::writeSeedFile(filename, seed_x, seed_y);
        
        // Verify
        assert(success && "writeSeedFile should succeed");
        assert(fileExists(filename) && "Output file should exist");
        assert(getFileSize(filename) > 0 && "Output file should not be empty");
        
        std::cout << "✓ writeSeedFile test PASSED" << std::endl;
        std::cout << "  File: " << filename << " (" << getFileSize(filename) << " bytes)" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ writeSeedFile test FAILED: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Test 4: writeTrialInfoFile()
// ============================================================================
bool test_writeTrialInfoFile() {
    std::cout << "\n=== Test 4: writeTrialInfoFile() ===" << std::endl;
    
    try {
        // Test data
        double fps = 30.0;
        std::vector<int> frame_indices;
        for (int i = 0; i < 150; ++i) {
            frame_indices.push_back(i);
        }
        
        // Write file
        std::string filename = TEST_OUTPUT_DIR + "/test_dic_info_data.mat";
        bool success = MatWriter::writeTrialInfoFile(filename, fps, frame_indices);
        
        // Verify
        assert(success && "writeTrialInfoFile should succeed");
        assert(fileExists(filename) && "Output file should exist");
        assert(getFileSize(filename) > 0 && "Output file should not be empty");
        
        std::cout << "✓ writeTrialInfoFile test PASSED" << std::endl;
        std::cout << "  File: " << filename << " (" << getFileSize(filename) << " bytes)" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ writeTrialInfoFile test FAILED: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Test 5: writeDIC2DPairResults()
// ============================================================================
bool test_writeDIC2DPairResults() {
    std::cout << "\n=== Test 5: writeDIC2DPairResults() ===" << std::endl;
    
    try {
        // Create test DIC2DPairResults
        DIC2DPairResults results;
        results.nCamRef = 1;
        results.nCamDef = 2;
        results.nImages = 10;
        
        // Create test ROI mask
        results.ROImask = cv::Mat::ones(100, 100, CV_8U);
        
        // Create test ncorrInfo
        results.ncorrInfo.cutoff_corrcoef = std::vector<double>(301, 0.5);
        results.ncorrInfo.cutoff_diffnorm = 0.01;
        results.ncorrInfo.cutoff_iteration = 50;
        results.ncorrInfo.imgcorr = {"ref.tif", "def.tif"};
        results.ncorrInfo.lenscoef = 0;
        results.ncorrInfo.pixtounits = 0.2;
        results.ncorrInfo.radius = 15;
        results.ncorrInfo.spacing = 5;
        results.ncorrInfo.subsettrunc = false;
        results.ncorrInfo.total_threads = 8;
        results.ncorrInfo.type = "2D";
        results.ncorrInfo.units = "mm";
        
        // Create test Points (10 frames)
        for (int i = 0; i < 10; ++i) {
            Points2D pts;
            for (int j = 0; j < 100; ++j) {
                pts.x.push_back(j * 1.0);
                pts.y.push_back(j * 0.5);
            }
            results.Points.push_back(pts);
        }
        
        // Create test CorCoeffVec (10 frames)
        for (int i = 0; i < 10; ++i) {
            std::vector<double> corr_coeffs;
            for (int j = 0; j < 100; ++j) {
                corr_coeffs.push_back(0.9 + 0.05 * (j % 10) / 10.0);
            }
            results.CorCoeffVec.push_back(corr_coeffs);
        }
        
        // Create test Faces
        for (int i = 0; i < 50; ++i) {
            results.Faces.push_back(i);
            results.Faces.push_back(i + 1);
            results.Faces.push_back(i + 2);
        }
        
        // Create test FaceColors
        for (int i = 0; i < 50; ++i) {
            results.FaceColors.push_back(128.0 + i);
        }
        
        // Write file
        std::string filename = TEST_OUTPUT_DIR + "/test_myDIC2DpairResults.mat";
        bool success = MatWriter::writeDIC2DPairResults(filename, results);
        
        // Verify
        assert(success && "writeDIC2DPairResults should succeed");
        assert(fileExists(filename) && "Output file should exist");
        assert(getFileSize(filename) > 0 && "Output file should not be empty");
        
        std::cout << "✓ writeDIC2DPairResults test PASSED" << std::endl;
        std::cout << "  File: " << filename << " (" << getFileSize(filename) << " bytes)" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ writeDIC2DPairResults test FAILED: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Test 6: write3DCombinedResults()
// ============================================================================
bool test_write3DCombinedResults() {
    std::cout << "\n=== Test 6: write3DCombinedResults() ===" << std::endl;
    
    try {
        // Create test DIC3Dcombined
        DIC3Dcombined combined;
        combined.pairIndices = {1, 2};
        
        // Create test Points3D (5 frames)
        for (int i = 0; i < 5; ++i) {
            Points3D pts;
            for (int j = 0; j < 50; ++j) {
                pts.x.push_back(j * 1.0);
                pts.y.push_back(j * 0.5);
                pts.z.push_back(j * 0.2);
            }
            combined.Points3D.push_back(pts);
        }
        
        // Create test Faces
        for (int i = 0; i < 30; ++i) {
            combined.Faces.push_back(i);
            combined.Faces.push_back(i + 1);
            combined.Faces.push_back(i + 2);
        }
        
        // Create test FaceColors
        for (int i = 0; i < 30; ++i) {
            combined.FaceColors.push_back(100.0 + i);
        }
        
        // Create test corrComb (5 frames)
        for (int i = 0; i < 5; ++i) {
            std::vector<double> corr;
            for (int j = 0; j < 50; ++j) {
                corr.push_back(0.85 + 0.1 * (j % 10) / 10.0);
            }
            combined.corrComb.push_back(corr);
        }
        
        // Create test FaceCorrComb (5 frames)
        for (int i = 0; i < 5; ++i) {
            std::vector<double> face_corr;
            for (int j = 0; j < 30; ++j) {
                face_corr.push_back(0.9);
            }
            combined.FaceCorrComb.push_back(face_corr);
        }
        
        // Create test FaceCentroids (5 frames)
        for (int i = 0; i < 5; ++i) {
            std::vector<double> centroids;
            for (int j = 0; j < 30; ++j) {
                centroids.push_back(j * 1.0);  // x
                centroids.push_back(j * 0.5);  // y
                centroids.push_back(j * 0.2);  // z
            }
            combined.FaceCentroids.push_back(centroids);
        }
        
        // Create test calibration data (2x1 format)
        combined.calibration.DLT_paths = {{"path/cam1.dlt"}, {"path/cam2.dlt"}};
        std::vector<double> dlt1 = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        std::vector<double> dlt2 = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
        combined.calibration.DLT_params = {{dlt1}, {dlt2}};
        
        // Create test distortion data (2x1 format)
        combined.distortion.distortion_models = {{"model1"}, {"model1"}};
        combined.distortion.distortion_paths = {{"path/cam1_dist.mat"}, {"path/cam2_dist.mat"}};
        
        // Create test FacePairInds and PointPairInds
        for (int i = 0; i < 30; ++i) {
            combined.FacePairInds.push_back(1);
        }
        for (int i = 0; i < 50; ++i) {
            combined.PointPairInds.push_back(1);
        }

        // Add one embedded DIC2D entry so nested MAT codec fields are exercised
        DIC2DPairResults dic2d;
        dic2d.nCamRef = 1;
        dic2d.nCamDef = 2;
        dic2d.nImages = 2;
        dic2d.pairOrder = {1, 2};
        dic2d.pairForced = true;
        dic2d.ROImask = cv::Mat::ones(8, 8, CV_8U);
        dic2d.ncorrInfo.cutoff_corrcoef = {0.5, 0.6};
        dic2d.ncorrInfo.type = "2D";
        dic2d.ncorrInfo.units = "mm";
        Points2D pts2d;
        pts2d.x = {0.0, 1.0};
        pts2d.y = {2.0, 3.0};
        dic2d.Points.push_back(pts2d);
        dic2d.CorCoeffVec.push_back({0.95, 0.96});
        dic2d.Faces = {0, 1, 1};
        dic2d.FaceColors = {1.0};
        combined.DIC2Dinfo.push_back(dic2d);

        // Add one embedded stereo-pair result so AllPairsResults is exercised
        DIC3DpairResults pair_result;
        pair_result.cameraPairInd = {1, 2};
        pair_result.DLTpath = {"path/cam1.dlt", "path/cam2.dlt"};
        pair_result.DLTparameters = {dlt1, dlt2};
        pair_result.distortionModel = {"model1", "model1"};
        pair_result.distortionPath = {"path/cam1_dist.mat", "path/cam2_dist.mat"};
        pair_result.Faces = {0, 1, 2};
        pair_result.FaceColors = {1.0};
        Points3D pair_pts;
        pair_pts.x = {1.0, 2.0};
        pair_pts.y = {3.0, 4.0};
        pair_pts.z = {5.0, 6.0};
        pair_result.Points3D.push_back(pair_pts);
        pair_result.Disp.DispVec.push_back({0.1, 0.2, 0.3, 0.4, 0.5, 0.6});
        pair_result.Disp.DispMgn.push_back({0.7, 0.8});
        pair_result.FaceCentroids.push_back({1.0, 2.0, 3.0});
        pair_result.corrComb.push_back({0.91, 0.92});
        pair_result.FaceCorrComb.push_back({0.93});
        combined.AllPairsResults.push_back(pair_result);
        
        // Write file
        std::string filename = TEST_OUTPUT_DIR + "/test_DIC3Dcombined.mat";
        bool success = MatWriter::write3DCombinedResults(filename, combined);
        
        // Verify
        assert(success && "write3DCombinedResults should succeed");
        assert(fileExists(filename) && "Output file should exist");
        assert(getFileSize(filename) > 0 && "Output file should not be empty");

        mat_t* matfp = Mat_Open(filename.c_str(), MAT_ACC_RDONLY);
        assert(matfp && "Written MAT file should be readable");
        matvar_t* combined_var = Mat_VarRead(matfp, "DIC3Dcombined");
        assert(combined_var && "DIC3Dcombined root struct should exist");
        assert(hasStructField(combined_var, "AllPairsResults") &&
               "DIC3Dcombined should contain AllPairsResults");
        assert(hasStructField(combined_var, "DIC2Dinfo") &&
               "DIC3Dcombined should contain DIC2Dinfo");
        matvar_t* all_pairs_var = Mat_VarGetStructFieldByName(combined_var, "AllPairsResults", 0);
        matvar_t* dic2d_var = Mat_VarGetStructFieldByName(combined_var, "DIC2Dinfo", 0);
        assert(all_pairs_var && all_pairs_var->class_type == MAT_C_CELL);
        assert(dic2d_var && dic2d_var->class_type == MAT_C_CELL);
        assert(Mat_VarGetCell(all_pairs_var, 0) != nullptr);
        assert(Mat_VarGetCell(dic2d_var, 0) != nullptr);
        Mat_VarFree(combined_var);
        Mat_Close(matfp);
        
        std::cout << "✓ write3DCombinedResults test PASSED" << std::endl;
        std::cout << "  File: " << filename << " (" << getFileSize(filename) << " bytes)" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ write3DCombinedResults test FAILED: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Test 7: write3DPPresults()
// ============================================================================
bool test_write3DPPresults() {
    std::cout << "\n=== Test 7: write3DPPresults() ===" << std::endl;
    
    try {
        // Create test DIC3DPPresults
        DIC3DPPresults ppresults;
        ppresults.pairIndices = {1, 2};
        
        // Create test Points3D (3 frames)
        for (int i = 0; i < 3; ++i) {
            Points3D pts;
            for (int j = 0; j < 40; ++j) {
                pts.x.push_back(j * 1.0);
                pts.y.push_back(j * 0.5);
                pts.z.push_back(j * 0.3);
            }
            ppresults.Points3D.push_back(pts);
        }
        
        // Create test Faces
        for (int i = 0; i < 25; ++i) {
            ppresults.Faces.push_back(i);
            ppresults.Faces.push_back(i + 1);
            ppresults.Faces.push_back(i + 2);
        }
        
        // Create test FaceColors
        for (int i = 0; i < 25; ++i) {
            ppresults.FaceColors.push_back(120.0 + i);
        }
        
        // Create test corrComb (3 frames)
        for (int i = 0; i < 3; ++i) {
            std::vector<double> corr;
            for (int j = 0; j < 40; ++j) {
                corr.push_back(0.88);
            }
            ppresults.corrComb.push_back(corr);
        }
        
        // Create test FaceCorrComb (3 frames)
        for (int i = 0; i < 3; ++i) {
            std::vector<double> face_corr;
            for (int j = 0; j < 25; ++j) {
                face_corr.push_back(0.92);
            }
            ppresults.FaceCorrComb.push_back(face_corr);
        }
        
        // Create test FaceCentroids (3 frames)
        for (int i = 0; i < 3; ++i) {
            std::vector<double> centroids;
            for (int j = 0; j < 25; ++j) {
                centroids.push_back(j * 1.0);
                centroids.push_back(j * 0.5);
                centroids.push_back(j * 0.3);
            }
            ppresults.FaceCentroids.push_back(centroids);
        }
        
        // Create test calibration data
        ppresults.calibration.DLT_paths = {{"path/cam1.dlt"}, {"path/cam2.dlt"}};
        std::vector<double> dlt1 = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        std::vector<double> dlt2 = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
        ppresults.calibration.DLT_params = {{dlt1}, {dlt2}};
        
        // Create test distortion data
        ppresults.distortion.distortion_models = {{"model1"}, {"model1"}};
        ppresults.distortion.distortion_paths = {{"path/cam1_dist.mat"}, {"path/cam2_dist.mat"}};
        
        // Create test FacePairInds and PointPairInds
        for (int i = 0; i < 25; ++i) {
            ppresults.FacePairInds.push_back(1);
        }
        for (int i = 0; i < 40; ++i) {
            ppresults.PointPairInds.push_back(1);
        }

        DIC2DPairResults dic2d;
        dic2d.nCamRef = 1;
        dic2d.nCamDef = 2;
        dic2d.nImages = 1;
        dic2d.ROImask = cv::Mat::ones(6, 6, CV_8U);
        dic2d.ncorrInfo.cutoff_corrcoef = {0.5};
        dic2d.ncorrInfo.type = "2D";
        dic2d.ncorrInfo.units = "mm";
        Points2D pts2d;
        pts2d.x = {1.0};
        pts2d.y = {2.0};
        dic2d.Points.push_back(pts2d);
        dic2d.CorCoeffVec.push_back({0.99});
        dic2d.Faces = {0, 0, 0};
        dic2d.FaceColors = {1.0};
        ppresults.DIC2Dinfo.push_back(dic2d);

        DIC3DpairResults pair_result;
        pair_result.cameraPairInd = {1, 2};
        pair_result.DLTpath = {"path/cam1.dlt", "path/cam2.dlt"};
        pair_result.DLTparameters = {dlt1, dlt2};
        pair_result.distortionModel = {"model1", "model1"};
        pair_result.distortionPath = {"path/cam1_dist.mat", "path/cam2_dist.mat"};
        pair_result.Faces = {0, 1, 2};
        pair_result.FaceColors = {1.0};
        Points3D pair_pts;
        pair_pts.x = {1.0};
        pair_pts.y = {2.0};
        pair_pts.z = {3.0};
        pair_result.Points3D.push_back(pair_pts);
        pair_result.Disp.DispVec.push_back({0.1, 0.2, 0.3});
        pair_result.Disp.DispMgn.push_back({0.4});
        pair_result.FaceCentroids.push_back({1.0, 2.0, 3.0});
        pair_result.corrComb.push_back({0.95});
        pair_result.FaceCorrComb.push_back({0.96});
        ppresults.AllPairsResults.push_back(pair_result);
        
        // Set deftype
        ppresults.deftype = "cum";
        ppresults.n_frames = 3;
        
        // Write file
        std::string filename = TEST_OUTPUT_DIR + "/test_DIC3DPPresults.mat";
        bool success = MatWriter::write3DPPresults(filename, ppresults);
        
        // Verify
        assert(success && "write3DPPresults should succeed");
        assert(fileExists(filename) && "Output file should exist");
        assert(getFileSize(filename) > 0 && "Output file should not be empty");

        mat_t* matfp = Mat_Open(filename.c_str(), MAT_ACC_RDONLY);
        assert(matfp && "Written MAT file should be readable");
        matvar_t* pp_var = Mat_VarRead(matfp, "DIC3DPPresults");
        assert(pp_var && "DIC3DPPresults root struct should exist");
        assert(hasStructField(pp_var, "AllPairsResults") &&
               "DIC3DPPresults should contain AllPairsResults");
        assert(hasStructField(pp_var, "DIC2Dinfo") &&
               "DIC3DPPresults should contain DIC2Dinfo");
        assert(hasStructField(pp_var, "deftype") &&
               "DIC3DPPresults should contain deftype");
        Mat_VarFree(pp_var);
        Mat_Close(matfp);
        
        std::cout << "✓ write3DPPresults test PASSED" << std::endl;
        std::cout << "  File: " << filename << " (" << getFileSize(filename) << " bytes)" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ write3DPPresults test FAILED: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Main Test Runner
// ============================================================================
int main(int argc, char** argv) {
    (void)argc;  // Suppress unused parameter warning
    (void)argv;  // Suppress unused parameter warning
    std::cout << "========================================" << std::endl;
    std::cout << "  MAT File Writers Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Setup
    setupTestDirectory();
    std::cout << "\nTest output directory: " << TEST_OUTPUT_DIR << std::endl;
    
    // Run all tests
    int passed = 0;
    int total = 7;
    
    if (test_writeMatchingFile()) passed++;
    if (test_writeROIMaskFile()) passed++;
    if (test_writeSeedFile()) passed++;
    if (test_writeTrialInfoFile()) passed++;
    if (test_writeDIC2DPairResults()) passed++;
    if (test_write3DCombinedResults()) passed++;
    if (test_write3DPPresults()) passed++;
    
    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Test Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Passed: " << passed << "/" << total << std::endl;
    std::cout << "Failed: " << (total - passed) << "/" << total << std::endl;
    
    if (passed == total) {
        std::cout << "\n✓ All tests PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗ Some tests FAILED!" << std::endl;
        return 1;
    }
}
