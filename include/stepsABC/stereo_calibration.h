/**
 * StepC: stereovision / DLT calibration
 *
 * C++ port of MATLAB MultiDIC main_script/stepC_stereovision_calibration.m and the
 * helpers it calls:
 *   - generate_cylindrical_calibration_object_coordinate_file.m
 *   - STEP1_CalcDLTparameters_rewrited.m  (per-camera DLT11 parameter fit)
 *   - the DLT reconstruction-error computation
 *
 * The 11-parameter DLT fit itself reuses the existing Utils::DLT11Calibration
 * helper (src/utils.cpp) so no new linear-algebra path is introduced. Eigen is
 * used for the stereo triangulation least-squares solve, consistent with the
 * rest of the repo.
 */

#ifndef STEPSABC_STEREO_CALIBRATION_H
#define STEPSABC_STEREO_CALIBRATION_H

#include <array>
#include <string>
#include <vector>

namespace cppxdic {
namespace stepsABC {

/** A single calibration point: 3D world coordinate (mm). */
struct CalibPoint3D {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/** A 2D image observation (pixels). */
struct CalibPoint2D {
    double u = 0.0;
    double v = 0.0;
};

/** Geometry of the cylindrical calibration object. */
struct CylinderCalibSpec {
    double radius = 30.0;    // cylinder radius (mm)
    int num_columns = 18;    // dots around the circumference
    int num_rows = 11;       // dot rows along the cylinder axis
    double dz = 5.0;         // vertical spacing between rows (mm)
    double theta0_deg = 0.0; // angular offset of first column (deg)
    bool full_circle = true; // columns span 360 deg (true) or [0, (N-1)*dtheta]
};

/** DLT parameters for one camera plus its fit residual. */
struct CameraDLT {
    int camera_id = 0;
    std::array<double, 11> L{};    // 11 DLT parameters
    double rms_reprojection = 0.0; // RMS image-plane reprojection error (px)
    bool valid = false;
};

/** Reconstruction-error metrics for a stereo pair. */
struct ReconstructionError {
    double mean = 0.0;             // mean 3D error magnitude (mm)
    double rms = 0.0;              // RMS 3D error magnitude (mm)
    double max = 0.0;              // max 3D error magnitude (mm)
    double std = 0.0;              // std-dev of 3D error magnitude (mm)
    std::vector<double> per_point; // per-point 3D error magnitude (mm)
};

/** Full StepC result for a stereo pair. */
struct StereoCalibrationResult {
    CameraDLT cam_first;
    CameraDLT cam_second;
    ReconstructionError recon_error;
    std::vector<CalibPoint3D> reconstructed; // triangulated 3D points
    bool valid = false;
};

class StereoCalibration {
public:
    /**
     * Port of generate_cylindrical_calibration_object_coordinate_file.m.
     * Generates the 3D coordinates of the calibration dots on the cylinder.
     * Ordering: outer loop over rows (z), inner loop over columns (theta) —
     * matching the MATLAB column-major dot ordering used by the marked images.
     */
    static std::vector<CalibPoint3D> generateCylindricalObject(const CylinderCalibSpec& spec);

    /** Write generated 3D coordinates to a text file (one "x y z" per line). */
    static bool writeObjectFile(const std::vector<CalibPoint3D>& pts, const std::string& path,
                                std::string& error_message);

    /** Read a "x y z" per line 3D coordinate file. */
    static std::vector<CalibPoint3D> readObjectFile(const std::string& path,
                                                    std::string& error_message);

    /** Read a "u v" per line 2D image-point file (one per calibration dot). */
    static std::vector<CalibPoint2D> readImagePointsFile(const std::string& path,
                                                         std::string& error_message);

    /**
     * Port of STEP1_CalcDLTparameters_rewrited.m for a single camera.
     * Fits the 11 DLT parameters from 2D<->3D correspondences (reusing
     * Utils::DLT11Calibration) and computes the RMS image reprojection error.
     */
    static CameraDLT calcDLTParameters(int camera_id, const std::vector<CalibPoint2D>& image_points,
                                       const std::vector<CalibPoint3D>& object_points,
                                       std::string& error_message);

    /**
     * Triangulate a 3D point from two cameras' DLT parameters and the matched
     * image observations (linear least-squares DLT triangulation, Eigen solve).
     */
    static CalibPoint3D reconstructPoint(const CameraDLT& cam1, const CalibPoint2D& p1,
                                         const CameraDLT& cam2, const CalibPoint2D& p2);

    /**
     * DLT reconstruction-error metric: triangulate every calibration point from
     * the two cameras and compare against the known object coordinates.
     */
    static ReconstructionError reconstructionError(const CameraDLT& cam1,
                                                   const std::vector<CalibPoint2D>& img1,
                                                   const CameraDLT& cam2,
                                                   const std::vector<CalibPoint2D>& img2,
                                                   const std::vector<CalibPoint3D>& object_points,
                                                   std::vector<CalibPoint3D>& reconstructed_out);

    /**
     * Full StepC driver for one stereo pair: fit both cameras and compute the
     * reconstruction error against the object coordinates.
     */
    static StereoCalibrationResult calibrateStereoPair(
        int cam_first_id, const std::vector<CalibPoint2D>& img_first, int cam_second_id,
        const std::vector<CalibPoint2D>& img_second, const std::vector<CalibPoint3D>& object_points,
        std::string& error_message);
};

} // namespace stepsABC
} // namespace cppxdic

#endif // STEPSABC_STEREO_CALIBRATION_H
