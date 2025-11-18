/**
 * Utility functions for CPPXDIC
 * Equivalent to various utility functions in Matlab xDIC
 */

#ifndef UTILS_H
#define UTILS_H

#include "config.h"
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

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
    
private:
    static bool checkROIReferences(const Config& config);
    static bool checkSeedReferences(const Config& config);
    static bool checkProtocolFiles(const Config& config);
    static bool checkCalibrationFiles(const Config& config);

    // Helpers for video
    static void getCamerasForPair(int stereopair, int& cam_first, int& cam_second);
};

#endif // UTILS_H
