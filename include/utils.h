/**
 * Utility functions for CPPXDIC
 * Equivalent to various utility functions in Matlab xDIC
 */

#ifndef UTILS_H
#define UTILS_H

#include "config.h"
#include "parameters.h"
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include <Eigen/Dense>

class Utils {
public:
    // Check data and protocol files (equivalent to dic_check.m)
    static bool dicCheck(const Config& config);
    
    // File system utilities
    static bool fileExists(const std::string& path);
    static bool directoryExists(const std::string& path);
    static std::vector<std::string> findFiles(const std::string& directory, 
                                            const std::string& pattern);
    
    // String utilities
    static std::string formatString(const std::string& format, ...);
    static std::vector<std::string> split(const std::string& str, char delimiter);

    // Video import (equivalents of import_vid.m / import_raw_vid.m)
    static bool importVid(const Config& config,
                          int trial,
                          int stereopair,
                          std::vector<std::string>& cam1Frames,
                          std::vector<std::string>& cam2Frames);

    static bool importRawVid(const Config& config,
                             int trial,
                             int stereopair,
                             int frameStart,
                             int frameEnd,
                             int frameJump,
                             std::vector<std::string>& cam1Frames,
                             std::vector<std::string>& cam2Frames);

    // ROI/SEED from MAT to JSON
    static bool loadROIFromMat(const Config& config,
                               int trial,
                               int stereopair,
                               std::string& roiJsonPath,
                               std::string& roiMaskImagePath);

    static bool loadSeedFromMat(const Config& config,
                                int trial,
                                int stereopair,
                                std::string& seedJsonPath);
    
    // Camera calibration and distortion removal
    struct CameraParameters {
        cv::Mat camera_matrix;      // 3x3 intrinsic matrix [fx 0 cx; 0 fy cy; 0 0 1]
        cv::Mat distortion_coeffs;  // Distortion coefficients [k1 k2 p1 p2 k3...]
        bool is_valid;
        
        CameraParameters() : is_valid(false) {}
    };
    
    /**
     * Load camera calibration parameters from MATLAB .mat file
     * Matches MATLAB: load('cameraCBparameters_cam_X.mat')
     * Expects structure 'cameraCBparameters' with field 'cameraParameters'
     */
    static bool loadCameraParameters(const std::string& mat_path, CameraParameters& params);
    
    /**
     * Undistort 2D points using camera parameters
     * Matches MATLAB: undistortPoints(points, cameraParameters)
     * @param points_in Input 2D points (Nx2)
     * @param params Camera calibration parameters
     * @param points_out Output undistorted 2D points (Nx2)
     */
    static void undistortPoints(const std::vector<cv::Point2d>& points_in,
                                const CameraParameters& params,
                                std::vector<cv::Point2d>& points_out);
    
    /**
     * Undistort single 2D point
     */
    static cv::Point2d undistortPoint(const cv::Point2d& point_in,
                                      const CameraParameters& params);
    
    /**
     * Compute DLT (Direct Linear Transformation) 11 parameters from 2D-3D point correspondences
     * Matches MATLAB: L = DLT11Calibration(P2, P3)
     * Solves the 2Nx11 least squares system M \ P2array for 11 DLT parameters
     * 
     * @param P2 2D image points (Nx2 row-major: [u1,v1, u2,v2, ...])
     * @param P3 3D world points (Nx3 row-major: [X1,Y1,Z1, X2,Y2,Z2, ...])
     * @param N Number of point correspondences
     * @param L Output: 11 DLT parameters
     * @return true if successful
     */
    static bool DLT11Calibration(const double* P2, const double* P3, size_t N,
                                  std::vector<double>& L);

    // Rigid Body Motion (RBM) transformation
    struct RigidTransform {
        Eigen::Matrix3d R;  // Rotation matrix
        Eigen::Vector3d t;  // Translation vector
        bool is_valid;
        
        RigidTransform() : R(Eigen::Matrix3d::Identity()), 
                          t(Eigen::Vector3d::Zero()), 
                          is_valid(false) {}
    };
    
    /**
     * Compute optimal rigid body transformation between two point clouds
     * Matches MATLAB: rigidTransformation(Pfrom, Pto)
     * Uses SVD-based least squares minimization
     * 
     * @param points_from Source point cloud to be transformed
     * @param points_to Target point cloud
     * @param transform Output: rotation R and translation t such that points_transformed = R * points_from + t
     * @return true if successful
     */
    static bool computeRigidTransform(const std::vector<Eigen::Vector3d>& points_from,
                                      const std::vector<Eigen::Vector3d>& points_to,
                                      RigidTransform& transform);
    
    /**
     * Apply rigid transformation to point cloud
     * points_out = R * points_in + t
     */
    static std::vector<Eigen::Vector3d> applyRigidTransform(const std::vector<Eigen::Vector3d>& points_in,
                                                             const RigidTransform& transform);
    
     /**
     * Get camera numbers for a given stereopair
     * @param stereopair The stereopair number (1 or 2)
     * @param cam_first Output: first camera number
     * @param cam_second Output: second camera number
     */
    static void getCamerasForPair(int stereopair, int& cam_first, int& cam_second);

     /**
     * Build base data path
     * @param config The base configuration class (required)
     * @param with_data_or_dic_path Whether to include data/dic path in the path (required)
     * @param with_rawdata Whether to include rawdata in the path (default as false)
     * @param with_speckles Whether to include speckles in the path (default as false)
     * @return The constructed path string
     */
    static std::string buildBaseDataPath(const Config& config, bool with_data_or_dic_path = false, bool with_rawdata = false, bool with_speckles = false);

    /**
     * Build the path based on activated parameters and return the proper string
     * @param config The base configuration class (required)
     * @param with_data_or_dic_path Whether to include data/dic path in the path (required)
     * @param with_rawdata Whether to include rawdata in the path (default as false)
     * @param with_speckles Whether to include speckles in the path (default as false)
     * @param with_material Whether to include material in the path (default as false)
     * @param with_video Whether to include video in the path (default as false)
     * @param with_protocol Whether to include protocol in the path (default as false)
     * @param with_calib Whether to include calib and calib folder set in the path (default as false)
     * @return The constructed path string
     */
    static std::string buildPath(const Config& config, bool with_data_or_dic_path = false, bool with_rawdata = false, bool with_speckles = false, bool with_material = false, bool with_video = false, bool with_protocol = false, bool with_calib = false);

     /**
     * Build protocol path
     * @param config The base configuration class (required)
     * @param with_data_or_dic_path Whether to include data/dic path in the path (required)
     * @param with_rawdata Whether to include rawdata in the path (default as false)
     * @param with_speckles Whether to include speckles in the path (default as false)
     * @param with_material Whether to include material in the path (default as false)
     * @return The constructed path string
     */
    static std::string buildProtocolDir(const Config& config, bool with_data_or_dic_path = false, bool with_rawdata = false, bool with_speckles = false, bool with_material = false);


    /**
     * Build the video path
     * @param config The base configuration class (required)
     * @param with_data_or_dic_path Whether to include data/dic path in the path (required)
     * @param with_rawdata Whether to include rawdata in the path (default as false)
     * @param with_speckles Whether to include speckles in the path (default as false)
     * @param with_material Whether to include material in the path (default as false)
     * @return The constructed path string
     */
    static std::string buildVideoDir(const Config& config, bool with_data_or_dic_path = false, bool with_rawdata = false, bool with_speckles = false, bool with_material = false);

    /**
     * Build the calib path
     * @param config The base configuration class (required)
     * @return The constructed path string
     */
    static std::string buildCalibDir(const Config& config);

    /**
     * Build the path based on activated parameters and return the proper string
     * @param parameters The base parameters structure (required)
     * @param with_material Whether to include material in the path (default as false)
     * @param with_trial Whether to include trial in the path (default as false)
     * @param with_phase Whether to include phase in the path (default as false)
     * @param with_cache Whether to include cache in the path (default as false)
     * @return The constructed path string
     */
    static std::string buildPath(const cppxdic::BaseParameters& parameters, bool with_material = false, bool with_trial = false, bool with_phase = false, bool with_cache = false);

    /**
     * Build the output path
     * @param parameters The base parameters structure (required)
     * @return The constructed path string
     */
    static std::string buildOutputPath(const cppxdic::BaseParameters& parameters);
    
    /**
     * Build the output cache path
     * @param parameters The base parameters structure (required)
     * @return The constructed path string
     */
    static std::string buildOutputCachePath(const cppxdic::BaseParameters& parameters);

    /**
     * Build the roi file path
     * @param parameters The base parameters structure (required)
     * @param reftrial The reference trial number (required)
     * @param stereopair The stereopair number (required)
     * @param extension The file extension (default as ".mat")
     * @return The constructed path string
     */
    static std::string buildRoiFilePath(const cppxdic::BaseParameters& parameters, std::string reftrial, int stereopair, const std::string& extension = ".mat");

    /**
     * Build the seed file path
     * @param parameters The base parameters structure (required)
     * @param reftrial The reference trial number (required)
     * @param stereopair The stereopair number (required)
     * @param extension The file extension (default as ".mat")
     * @return The constructed path string
     */
    static std::string buildSeedFilePath(const cppxdic::BaseParameters& parameters, std::string reftrial, int stereopair, const std::string& extension = ".mat");


    /**
     * Build the matching file path
     * @param parameters The base parameters structure (required)
     * @param reftrial The reference trial number (required)
     * @param stereopair The stereopair number (required)
     * @param extension The file extension (default as ".mat")
     * @return The constructed path string
     */
    static std::string buildMatchingFilePath(const cppxdic::BaseParameters& parameters, std::string reftrial, int stereopair, const std::string& extension = ".bin");

private:
    static bool checkROIReferences(const Config& config);
    static bool checkSeedReferences(const Config& config);
    static bool checkProtocolFiles(const Config& config);
    static bool checkCalibrationFiles(const Config& config);

    static std::string buildRoiOrSeedLikeFilePath(const cppxdic::BaseParameters& parameters, std::string reftrial, int stereopair, std::string_view prefix, const std::string& extension = ".mat");

};

#endif // UTILS_H
