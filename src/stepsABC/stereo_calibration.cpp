/**
 * Implementation of StepC stereovision / DLT calibration.
 *
 * Ported from MATLAB MultiDIC stepC_stereovision_calibration.m. The 11-parameter
 * DLT fit reuses Utils::DLT11Calibration; the stereo triangulation uses Eigen
 * (consistent with the rest of the repo).
 */

#include "stepsABC/stereo_calibration.h"
#include "utils.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace cppxdic {
namespace stepsABC {

// TODO(stepC): the MATLAB stepC script also derives the per-camera 2D image
// points by extracting the calibration markers from the marked calibration
// images (camera_info_from_view / marked-image parsing) and manages global
// paths/settings via theGlobalSettings_MNG. This port takes the 2D image points
// as pre-extracted input files (readImagePointsFile) instead of re-deriving them
// from the marked images. Automatic marker extraction is a deliberate gap and a
// follow-up; the DLT fit and reconstruction-error metric below are full ports.
std::vector<CalibPoint3D> StereoCalibration::generateCylindricalObject(
    const CylinderCalibSpec& spec) {
    std::vector<CalibPoint3D> pts;
    pts.reserve(static_cast<size_t>(std::max(0, spec.num_rows)) *
                static_cast<size_t>(std::max(0, spec.num_columns)));

    // Angular step around the cylinder. For a full circle the columns wrap all
    // the way around (dtheta = 2*pi/Ncol); otherwise they span an open arc.
    const double two_pi = 2.0 * M_PI;
    double dtheta = 0.0;
    if (spec.num_columns > 0) {
        dtheta = spec.full_circle ? (two_pi / spec.num_columns)
                                  : (spec.num_columns > 1 ? (two_pi / spec.num_columns) : 0.0);
    }
    const double theta0 = spec.theta0_deg * M_PI / 180.0;

    // MATLAB ordering: outer loop over rows (z), inner over columns (theta).
    for (int r = 0; r < spec.num_rows; ++r) {
        const double z = r * spec.dz;
        for (int c = 0; c < spec.num_columns; ++c) {
            const double theta = theta0 + c * dtheta;
            CalibPoint3D p;
            p.x = spec.radius * std::cos(theta);
            p.y = spec.radius * std::sin(theta);
            p.z = z;
            pts.push_back(p);
        }
    }
    return pts;
}

bool StereoCalibration::writeObjectFile(const std::vector<CalibPoint3D>& pts,
                                        const std::string& path, std::string& error_message) {
    std::ofstream ofs(path);
    if (!ofs) {
        error_message = "Cannot open for writing: " + path;
        return false;
    }
    ofs << "# xdic cylindrical calibration object v1 (x y z, mm)\n";
    for (const auto& p : pts) {
        ofs << p.x << " " << p.y << " " << p.z << "\n";
    }
    return true;
}

std::vector<CalibPoint3D> StereoCalibration::readObjectFile(const std::string& path,
                                                            std::string& error_message) {
    std::vector<CalibPoint3D> pts;
    std::ifstream ifs(path);
    if (!ifs) {
        error_message = "Cannot open object file: " + path;
        return pts;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        CalibPoint3D p;
        if (iss >> p.x >> p.y >> p.z) pts.push_back(p);
    }
    return pts;
}

std::vector<CalibPoint2D> StereoCalibration::readImagePointsFile(const std::string& path,
                                                                 std::string& error_message) {
    std::vector<CalibPoint2D> pts;
    std::ifstream ifs(path);
    if (!ifs) {
        error_message = "Cannot open image-points file: " + path;
        return pts;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        CalibPoint2D p;
        if (iss >> p.u >> p.v) pts.push_back(p);
    }
    return pts;
}

CameraDLT StereoCalibration::calcDLTParameters(int camera_id,
                                               const std::vector<CalibPoint2D>& image_points,
                                               const std::vector<CalibPoint3D>& object_points,
                                               std::string& error_message) {
    CameraDLT cam;
    cam.camera_id = camera_id;

    if (image_points.size() != object_points.size()) {
        error_message = "calcDLTParameters: image/object point count mismatch (" +
                        std::to_string(image_points.size()) + " vs " +
                        std::to_string(object_points.size()) + ")";
        return cam;
    }
    const size_t N = image_points.size();
    if (N < 6) {
        error_message = "calcDLTParameters: need >=6 correspondences, got " + std::to_string(N);
        return cam;
    }

    // Flatten into the row-major layout expected by Utils::DLT11Calibration.
    std::vector<double> P2(N * 2);
    std::vector<double> P3(N * 3);
    for (size_t i = 0; i < N; ++i) {
        P2[i * 2 + 0] = image_points[i].u;
        P2[i * 2 + 1] = image_points[i].v;
        P3[i * 3 + 0] = object_points[i].x;
        P3[i * 3 + 1] = object_points[i].y;
        P3[i * 3 + 2] = object_points[i].z;
    }

    std::vector<double> L;
    if (!Utils::DLT11Calibration(P2.data(), P3.data(), N, L) || L.size() != 11) {
        error_message =
            "calcDLTParameters: DLT11Calibration failed for camera " + std::to_string(camera_id);
        return cam;
    }
    for (int i = 0; i < 11; ++i) cam.L[i] = L[i];

    // RMS image-plane reprojection error.
    // u = (L1 X + L2 Y + L3 Z + L4) / (L9 X + L10 Y + L11 Z + 1)
    // v = (L5 X + L6 Y + L7 Z + L8) / (L9 X + L10 Y + L11 Z + 1)
    double sse = 0.0;
    for (size_t i = 0; i < N; ++i) {
        const double X = object_points[i].x;
        const double Y = object_points[i].y;
        const double Z = object_points[i].z;
        const double den = cam.L[8] * X + cam.L[9] * Y + cam.L[10] * Z + 1.0;
        if (std::abs(den) < 1e-12) continue;
        const double u = (cam.L[0] * X + cam.L[1] * Y + cam.L[2] * Z + cam.L[3]) / den;
        const double v = (cam.L[4] * X + cam.L[5] * Y + cam.L[6] * Z + cam.L[7]) / den;
        const double du = u - image_points[i].u;
        const double dv = v - image_points[i].v;
        sse += du * du + dv * dv;
    }
    cam.rms_reprojection = std::sqrt(sse / static_cast<double>(N));
    cam.valid = true;
    return cam;
}

CalibPoint3D StereoCalibration::reconstructPoint(const CameraDLT& c1, const CalibPoint2D& p1,
                                                 const CameraDLT& c2, const CalibPoint2D& p2) {
    // Linear DLT triangulation. For each camera and observation (u, v):
    //   (u*L9 - L1) X + (u*L10 - L2) Y + (u*L11 - L3) Z = L4 - u
    //   (v*L9 - L5) X + (v*L10 - L6) Y + (v*L11 - L7) Z = L8 - v
    // Stack the two cameras -> 4x3 least-squares system A*[X Y Z]^T = b.
    Eigen::Matrix<double, 4, 3> A;
    Eigen::Matrix<double, 4, 1> b;

    auto fill = [](const CameraDLT& c, const CalibPoint2D& p, Eigen::Matrix<double, 4, 3>& A,
                   Eigen::Matrix<double, 4, 1>& b, int row) {
        A(row, 0) = p.u * c.L[8] - c.L[0];
        A(row, 1) = p.u * c.L[9] - c.L[1];
        A(row, 2) = p.u * c.L[10] - c.L[2];
        b(row) = c.L[3] - p.u;
        A(row + 1, 0) = p.v * c.L[8] - c.L[4];
        A(row + 1, 1) = p.v * c.L[9] - c.L[5];
        A(row + 1, 2) = p.v * c.L[10] - c.L[6];
        b(row + 1) = c.L[7] - p.v;
    };

    fill(c1, p1, A, b, 0);
    fill(c2, p2, A, b, 2);

    Eigen::Vector3d X = A.colPivHouseholderQr().solve(b);
    CalibPoint3D out;
    out.x = X(0);
    out.y = X(1);
    out.z = X(2);
    return out;
}

ReconstructionError StereoCalibration::reconstructionError(
    const CameraDLT& cam1, const std::vector<CalibPoint2D>& img1, const CameraDLT& cam2,
    const std::vector<CalibPoint2D>& img2, const std::vector<CalibPoint3D>& object_points,
    std::vector<CalibPoint3D>& reconstructed_out) {
    ReconstructionError err;
    reconstructed_out.clear();

    const size_t N = std::min({img1.size(), img2.size(), object_points.size()});
    if (N == 0) return err;

    reconstructed_out.reserve(N);
    err.per_point.reserve(N);

    double sum = 0.0;
    double sum_sq = 0.0;
    for (size_t i = 0; i < N; ++i) {
        CalibPoint3D rec = reconstructPoint(cam1, img1[i], cam2, img2[i]);
        reconstructed_out.push_back(rec);
        const double dx = rec.x - object_points[i].x;
        const double dy = rec.y - object_points[i].y;
        const double dz = rec.z - object_points[i].z;
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        err.per_point.push_back(d);
        sum += d;
        sum_sq += d * d;
        if (d > err.max) err.max = d;
    }
    const double n = static_cast<double>(N);
    err.mean = sum / n;
    err.rms = std::sqrt(sum_sq / n);
    double var = 0.0;
    for (double d : err.per_point) var += (d - err.mean) * (d - err.mean);
    err.std = std::sqrt(var / n);
    return err;
}

StereoCalibrationResult StereoCalibration::calibrateStereoPair(
    int cam_first_id, const std::vector<CalibPoint2D>& img_first, int cam_second_id,
    const std::vector<CalibPoint2D>& img_second, const std::vector<CalibPoint3D>& object_points,
    std::string& error_message) {
    StereoCalibrationResult result;

    result.cam_first = calcDLTParameters(cam_first_id, img_first, object_points, error_message);
    if (!result.cam_first.valid) return result;

    result.cam_second = calcDLTParameters(cam_second_id, img_second, object_points, error_message);
    if (!result.cam_second.valid) return result;

    result.recon_error = reconstructionError(result.cam_first, img_first, result.cam_second,
                                             img_second, object_points, result.reconstructed);
    result.valid = true;
    return result;
}

} // namespace stepsABC
} // namespace cppxdic
