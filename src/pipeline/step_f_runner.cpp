#include "cppxdic/pipeline/step_f_runner.h"

#include "data_serializer.h"
#include "dic_structures.h"
#include "face_isotropy.h"
#include "strain_computation.h"
#include "temporal_filter.h"
#include "utils.h"
#include "visualization.h"
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <vector>

namespace cppxdic::pipeline {

namespace {

using VertexFrame = std::vector<Eigen::Vector3d>;
using VertexSeries = std::vector<VertexFrame>;

VertexFrame buildReferenceVertices(const DIC3Dcombined& dic3d) {
    VertexFrame vertices_ref;
    const size_t point_count = dic3d.Points3D[0].x.size();
    vertices_ref.reserve(point_count);
    for (size_t i = 0; i < point_count; ++i) {
        vertices_ref.emplace_back(dic3d.Points3D[0].x[i], dic3d.Points3D[0].y[i], dic3d.Points3D[0].z[i]);
    }
    return vertices_ref;
}

VertexSeries buildAllVertices(const DIC3Dcombined& dic3d) {
    VertexSeries frames;
    frames.reserve(dic3d.Points3D.size());
    for (const auto& frame_pts : dic3d.Points3D) {
        VertexFrame frame;
        frame.reserve(frame_pts.x.size());
        for (size_t i = 0; i < frame_pts.x.size(); ++i) {
            frame.emplace_back(frame_pts.x[i], frame_pts.y[i], frame_pts.z[i]);
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

std::vector<size_t> countNanPoints(const VertexSeries& frames) {
    std::vector<size_t> counts;
    counts.reserve(frames.size());
    for (const auto& frame : frames) {
        size_t nan_count = 0;
        for (const auto& point : frame) {
            if (point.hasNaN()) {
                nan_count++;
            }
        }
        counts.push_back(nan_count);
    }
    return counts;
}

size_t countPointsWithAnyNan(const VertexSeries& frames) {
    if (frames.empty()) {
        return 0;
    }
    const size_t point_count = frames.front().size();
    size_t bad_points = 0;
    for (size_t point_index = 0; point_index < point_count; ++point_index) {
        bool any_nan = false;
        for (const auto& frame : frames) {
            if (point_index >= frame.size() || frame[point_index].hasNaN()) {
                any_nan = true;
                break;
            }
        }
        if (any_nan) {
            bad_points++;
        }
    }
    return bad_points;
}

size_t countValidFaces(const std::vector<int>& faces, const VertexFrame& vertices) {
    const size_t face_count = faces.size() / 3;
    size_t valid_faces = 0;
    for (size_t iface = 0; iface < face_count; ++iface) {
        const int v0 = faces[iface * 3];
        const int v1 = faces[iface * 3 + 1];
        const int v2 = faces[iface * 3 + 2];
        if (v0 < 0 || v1 < 0 || v2 < 0) {
            continue;
        }
        if (static_cast<size_t>(v0) >= vertices.size() ||
            static_cast<size_t>(v1) >= vertices.size() ||
            static_cast<size_t>(v2) >= vertices.size()) {
            continue;
        }
        if (vertices[v0].hasNaN() || vertices[v1].hasNaN() || vertices[v2].hasNaN()) {
            continue;
        }
        valid_faces++;
    }
    return valid_faces;
}

double computeMaxDisplacement(const VertexSeries& frames, const VertexFrame& reference) {
    double max_disp = 0.0;
    for (const auto& frame : frames) {
        const size_t n_local = std::min(frame.size(), reference.size());
        for (size_t point_index = 0; point_index < n_local; ++point_index) {
            if (!frame[point_index].allFinite() || !reference[point_index].allFinite()) {
                continue;
            }
            max_disp = std::max(max_disp, (frame[point_index] - reference[point_index]).norm());
        }
    }
    return max_disp;
}

void rebuildPoints3D(DIC3Dcombined& dic3d, const VertexSeries& frames) {
    dic3d.Points3D.clear();
    dic3d.Points3D.resize(frames.size());
    const size_t point_count = frames.empty() ? 0 : frames.front().size();
    for (size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
        auto& frame_pts = dic3d.Points3D[frame_index];
        frame_pts.x.resize(point_count);
        frame_pts.y.resize(point_count);
        frame_pts.z.resize(point_count);
        for (size_t point_index = 0; point_index < point_count; ++point_index) {
            frame_pts.x[point_index] = frames[frame_index][point_index].x();
            frame_pts.y[point_index] = frames[frame_index][point_index].y();
            frame_pts.z[point_index] = frames[frame_index][point_index].z();
        }
    }
}

void recomputeDisplacement(DIC3Dcombined& dic3d, const VertexSeries& frames, const VertexFrame& reference) {
    dic3d.Disp.DispVec.clear();
    dic3d.Disp.DispMgn.clear();
    dic3d.Disp.DispVec.resize(frames.size());
    dic3d.Disp.DispMgn.resize(frames.size());

    const size_t point_count = reference.size();
    for (size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
        auto& disp_vec = dic3d.Disp.DispVec[frame_index];
        auto& disp_mgn = dic3d.Disp.DispMgn[frame_index];
        disp_vec.resize(point_count * 3);
        disp_mgn.resize(point_count);

        for (size_t point_index = 0; point_index < point_count; ++point_index) {
            const double dx = frames[frame_index][point_index].x() - reference[point_index].x();
            const double dy = frames[frame_index][point_index].y() - reference[point_index].y();
            const double dz = frames[frame_index][point_index].z() - reference[point_index].z();

            disp_vec[point_index * 3] = dx;
            disp_vec[point_index * 3 + 1] = dy;
            disp_vec[point_index * 3 + 2] = dz;
            disp_mgn[point_index] = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }
}

void recomputeFaceCentroids(DIC3Dcombined& dic3d, const VertexSeries& frames) {
    const size_t face_count = dic3d.Faces.size() / 3;
    dic3d.FaceCentroids.clear();
    dic3d.FaceCentroids.resize(frames.size());

    for (size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
        auto& centroids = dic3d.FaceCentroids[frame_index];
        centroids.resize(face_count * 3);
        const size_t vertex_count = frames[frame_index].size();
        for (size_t iface = 0; iface < face_count; ++iface) {
            const int v0 = dic3d.Faces[iface * 3];
            const int v1 = dic3d.Faces[iface * 3 + 1];
            const int v2 = dic3d.Faces[iface * 3 + 2];

            if (v0 >= 0 && static_cast<size_t>(v0) < vertex_count &&
                v1 >= 0 && static_cast<size_t>(v1) < vertex_count &&
                v2 >= 0 && static_cast<size_t>(v2) < vertex_count) {
                centroids[iface * 3] = (frames[frame_index][v0].x() + frames[frame_index][v1].x() + frames[frame_index][v2].x()) / 3.0;
                centroids[iface * 3 + 1] = (frames[frame_index][v0].y() + frames[frame_index][v1].y() + frames[frame_index][v2].y()) / 3.0;
                centroids[iface * 3 + 2] = (frames[frame_index][v0].z() + frames[frame_index][v1].z() + frames[frame_index][v2].z()) / 3.0;
            } else {
                centroids[iface * 3] = std::numeric_limits<double>::quiet_NaN();
                centroids[iface * 3 + 1] = std::numeric_limits<double>::quiet_NaN();
                centroids[iface * 3 + 2] = std::numeric_limits<double>::quiet_NaN();
            }
        }
    }
}

void recomputeFaceCorrelation(DIC3Dcombined& dic3d, const VertexSeries& frames) {
    if (dic3d.corrComb.empty() || dic3d.corrComb.size() < frames.size()) {
        std::cout << "  \u26a0 corrComb not available (empty or frame count mismatch), skipping face correlation" << std::endl;
        return;
    }

    const size_t face_count = dic3d.Faces.size() / 3;
    dic3d.FaceCorrComb.clear();
    dic3d.FaceCorrComb.resize(frames.size());

    for (size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
        auto& face_corr = dic3d.FaceCorrComb[frame_index];
        face_corr.resize(face_count);
        const auto& point_corr = dic3d.corrComb[frame_index];
        const size_t corr_count = point_corr.size();

        for (size_t iface = 0; iface < face_count; ++iface) {
            const int v0 = dic3d.Faces[iface * 3];
            const int v1 = dic3d.Faces[iface * 3 + 1];
            const int v2 = dic3d.Faces[iface * 3 + 2];
            if (v0 >= 0 && static_cast<size_t>(v0) < corr_count &&
                v1 >= 0 && static_cast<size_t>(v1) < corr_count &&
                v2 >= 0 && static_cast<size_t>(v2) < corr_count) {
                face_corr[iface] = std::max({point_corr[v0], point_corr[v1], point_corr[v2]});
            } else {
                face_corr[iface] = std::numeric_limits<double>::quiet_NaN();
            }
        }
    }
    std::cout << "  \u2713 Face correlation recomputed for " << frames.size() << " frames" << std::endl;
}

void copyCombinedFields(DIC3DPPresults& results, const DIC3Dcombined& combined) {
    results.pairIndices = combined.pairIndices;
    results.Points3D = combined.Points3D;
    results.Faces = combined.Faces;
    results.FaceColors = combined.FaceColors;
    results.corrComb = combined.corrComb;
    results.FaceCorrComb = combined.FaceCorrComb;
    results.FaceCentroids = combined.FaceCentroids;
    results.Disp = combined.Disp;
    results.calibration = combined.calibration;
    results.distortion = combined.distortion;
    results.FacePairInds = combined.FacePairInds;
    results.PointPairInds = combined.PointPairInds;
    results.DIC2Dinfo = combined.DIC2Dinfo;
    results.AllPairsResults = combined.AllPairsResults;
}

void populateDeformData(DeformData& target, const FrameDeformationResult& source, size_t face_count) {
    target.F.resize(source.n_frames);
    target.strain.resize(source.n_frames);
    target.princStrain.resize(source.n_frames);
    target.maxShearStrain.resize(source.n_frames);

    for (size_t frame_index = 0; frame_index < source.n_frames; ++frame_index) {
        const auto& frame = source.frames[frame_index];

        DeformGradient& f = target.F[frame_index];
        f.F11.resize(face_count); f.F12.resize(face_count); f.F13.resize(face_count);
        f.F21.resize(face_count); f.F22.resize(face_count); f.F23.resize(face_count);
        f.F31.resize(face_count); f.F32.resize(face_count); f.F33.resize(face_count);

        StrainTensor& e = target.strain[frame_index];
        e.E11.resize(face_count); e.E12.resize(face_count); e.E13.resize(face_count);
        e.E21.resize(face_count); e.E22.resize(face_count); e.E23.resize(face_count);
        e.E31.resize(face_count); e.E32.resize(face_count); e.E33.resize(face_count);

        for (size_t iface = 0; iface < face_count; ++iface) {
            f.F11[iface] = frame.Fmat[iface](0, 0);
            f.F12[iface] = frame.Fmat[iface](0, 1);
            f.F13[iface] = frame.Fmat[iface](0, 2);
            f.F21[iface] = frame.Fmat[iface](1, 0);
            f.F22[iface] = frame.Fmat[iface](1, 1);
            f.F23[iface] = frame.Fmat[iface](1, 2);
            f.F31[iface] = frame.Fmat[iface](2, 0);
            f.F32[iface] = frame.Fmat[iface](2, 1);
            f.F33[iface] = frame.Fmat[iface](2, 2);

            e.E11[iface] = frame.Emat[iface](0, 0);
            e.E12[iface] = frame.Emat[iface](0, 1);
            e.E13[iface] = frame.Emat[iface](0, 2);
            e.E21[iface] = frame.Emat[iface](1, 0);
            e.E22[iface] = frame.Emat[iface](1, 1);
            e.E23[iface] = frame.Emat[iface](1, 2);
            e.E31[iface] = frame.Emat[iface](2, 0);
            e.E32[iface] = frame.Emat[iface](2, 1);
            e.E33[iface] = frame.Emat[iface](2, 2);
        }

        auto& principal = target.princStrain[frame_index];
        principal.resize(face_count * 2);
        for (size_t iface = 0; iface < face_count; ++iface) {
            principal[iface * 2] = frame.Epc1[iface];
            principal[iface * 2 + 1] = frame.Epc2[iface];
        }
        target.maxShearStrain[frame_index] = frame.EShearMax;
    }
}

void populateRbmData(DIC3DPPresults& results, const std::vector<Utils::RigidTransform>& transforms,
                     const VertexSeries& vertices_after_rbm) {
    results.RBM.RotMat.resize(transforms.size());
    results.RBM.TransVec.resize(transforms.size());
    for (size_t frame_index = 0; frame_index < transforms.size(); ++frame_index) {
        const auto& transform = transforms[frame_index];
        results.RBM.RotMat[frame_index].resize(9);
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                results.RBM.RotMat[frame_index][i * 3 + j] = transform.R(i, j);
            }
        }
        results.RBM.TransVec[frame_index] = {transform.t(0), transform.t(1), transform.t(2)};
    }

    results.Points3D_ARBM_x.resize(vertices_after_rbm.size());
    results.Points3D_ARBM_y.resize(vertices_after_rbm.size());
    results.Points3D_ARBM_z.resize(vertices_after_rbm.size());
    for (size_t frame_index = 0; frame_index < vertices_after_rbm.size(); ++frame_index) {
        const auto& verts = vertices_after_rbm[frame_index];
        results.Points3D_ARBM_x[frame_index].resize(verts.size());
        results.Points3D_ARBM_y[frame_index].resize(verts.size());
        results.Points3D_ARBM_z[frame_index].resize(verts.size());
        for (size_t i = 0; i < verts.size(); ++i) {
            results.Points3D_ARBM_x[frame_index][i] = verts[i].x();
            results.Points3D_ARBM_y[frame_index][i] = verts[i].y();
            results.Points3D_ARBM_z[frame_index][i] = verts[i].z();
        }
    }
}

void exportVisualizationIfEnabled(const Config& config, DIC3DPPresults& results, const std::string& output_dir) {
    if (!config.mapLogic) {
        return;
    }

    std::cout << "\n=== Generating Visualization Exports ===" << std::endl;
    try {
        Visualization viz(config);
        viz.printTrialInfo(results);
        viz.applyTemporalFilter(results);
        auto vis_data = viz.prepareVisualizationData(results);

        std::filesystem::create_directories(output_dir + "viz");
        viz.exportData(vis_data, Utils::buildVizPath(output_dir, "trial.txt"));
        viz.generateSummaryStats(results, Utils::buildVizPath(output_dir, "trial_summary.txt"));

        if (config.generate_videos) {
            std::cout << "\n--- Generating Videos ---" << std::endl;
            for (const auto& field : config.plotopt) {
                viz.generateVideo(vis_data, output_dir + "viz/" + field + "_video.avi", field);
            }
            if (std::find(config.plotopt.begin(), config.plotopt.end(), "DispMgn") == config.plotopt.end()) {
                if (vis_data.FaceScalars.count("DispMgn") || !results.Disp.DispMgn.empty()) {
                    viz.generateVideo(vis_data, output_dir + "viz/DispMgn_video.avi", "DispMgn");
                }
            }
        }

        std::cout << "\u2713 Visualization exports complete" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Warning: Visualization export failed: " << e.what() << std::endl;
        std::cerr << "  (Continuing anyway...)" << std::endl;
    }
}

} // namespace

StepFRunner::StepFRunner(const Config& config)
    : config_(config) {}

bool StepFRunner::run(const std::vector<int>& trial_target) const {
    std::cout << "Starting Deformation/Strain Analysis (Step F)..." << std::endl;
    std::cout << "NOTE: This requires DIC3Dcombined from Step E" << std::endl;

    try {
        for (int trial : trial_target) {
            const std::string output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);

            auto serializer = DataSerializer::create(config_.data_format);
            const std::string dic3d_file =
                Utils::buildDic3DCombinedFilePath(output_dir, config_.num_pair, serializer->extension());

            if (!std::filesystem::exists(dic3d_file)) {
                std::cerr << "ERROR: DIC3Dcombined file not found: " << dic3d_file << std::endl;
                std::cerr << "You must run Step E (dic3DReconstruction) first!" << std::endl;
                return false;
            }

            std::cout << "Loading DIC3Dcombined from: " << dic3d_file << std::endl;

            DIC3Dcombined dic3d;
            if (!serializer->loadDIC3Dcombined(dic3d_file, dic3d)) {
                std::cerr << "Failed to load DIC3Dcombined structure" << std::endl;
                return false;
            }

            if (dic3d.Points3D.empty() || dic3d.Faces.empty()) {
                std::cerr << "DIC3Dcombined has no 3D data" << std::endl;
                return false;
            }

            std::cout << "  Loaded: " << dic3d.Points3D.size() << " frames, "
                      << dic3d.Points3D[0].x.size() << " points, "
                      << dic3d.Faces.size() / 3 << " faces" << std::endl;
            std::cout << "  corrComb: " << dic3d.corrComb.size() << " frames"
                      << (dic3d.corrComb.empty() ? "" : " (" + std::to_string(dic3d.corrComb[0].size()) + " per frame)")
                      << std::endl;
            std::cout << "  FaceCorrComb: " << dic3d.FaceCorrComb.size() << " frames" << std::endl;
            std::cout << "  FaceCentroids: " << dic3d.FaceCentroids.size() << " frames" << std::endl;
            std::cout << "  Disp.DispVec: " << dic3d.Disp.DispVec.size() << " frames" << std::endl;
            std::cout << "  Disp.DispMgn: " << dic3d.Disp.DispMgn.size() << " frames" << std::endl;

            {
                size_t inf_pts_total = 0;
                size_t bad_face_total = 0;
                const size_t point_count = dic3d.Points3D.empty() ? 0 : dic3d.Points3D[0].x.size();
                for (size_t frame_index = 0; frame_index < std::min(dic3d.Points3D.size(), size_t(3)); ++frame_index) {
                    size_t nan_pts = 0;
                    for (size_t i = 0; i < point_count; ++i) {
                        if (std::isnan(dic3d.Points3D[frame_index].x[i]) ||
                            std::isnan(dic3d.Points3D[frame_index].y[i]) ||
                            std::isnan(dic3d.Points3D[frame_index].z[i])) {
                            nan_pts++;
                        }
                        if (std::isinf(dic3d.Points3D[frame_index].x[i]) ||
                            std::isinf(dic3d.Points3D[frame_index].y[i]) ||
                            std::isinf(dic3d.Points3D[frame_index].z[i])) {
                            inf_pts_total++;
                        }
                    }
                    std::cout << "  [diag] Frame " << frame_index << ": " << nan_pts
                              << "/" << point_count << " NaN points" << std::endl;
                }
                const size_t face_count = dic3d.Faces.size() / 3;
                for (size_t i = 0; i < face_count; ++i) {
                    const int v0 = dic3d.Faces[i * 3];
                    const int v1 = dic3d.Faces[i * 3 + 1];
                    const int v2 = dic3d.Faces[i * 3 + 2];
                    if (v0 < 0 || v1 < 0 || v2 < 0 ||
                        static_cast<size_t>(v0) >= point_count ||
                        static_cast<size_t>(v1) >= point_count ||
                        static_cast<size_t>(v2) >= point_count) {
                        bad_face_total++;
                    }
                }
                if (bad_face_total > 0) {
                    std::cerr << "  [diag] WARNING: " << bad_face_total << "/" << face_count
                              << " faces have out-of-bounds vertex indices!" << std::endl;
                }
                if (inf_pts_total > 0) {
                    std::cerr << "  [diag] WARNING: " << inf_pts_total << " Inf values in Points3D!" << std::endl;
                }
            }

            std::cout << "\nConverting data to Eigen format..." << std::endl;
            VertexFrame vertices_ref = buildReferenceVertices(dic3d);
            VertexSeries vertices_all_frames = buildAllVertices(dic3d);
            const size_t point_count = vertices_ref.size();
            const size_t face_count = dic3d.Faces.size() / 3;

            std::cout << "  Converted " << vertices_all_frames.size() << " frames" << std::endl;

            const size_t raw_any_nan_points = countPointsWithAnyNan(vertices_all_frames);
            const auto raw_nan_counts = countNanPoints(vertices_all_frames);
            const double raw_max_disp = computeMaxDisplacement(vertices_all_frames, vertices_ref);
            std::cout << "  Raw points with any NaN over time: " << raw_any_nan_points
                      << "/" << point_count << std::endl;
            if (!raw_nan_counts.empty()) {
                std::cout << "  Raw NaN points in frame 0: " << raw_nan_counts[0]
                          << "/" << point_count << std::endl;
            }
            std::cout << "  Raw valid faces in frame 0: "
                      << countValidFaces(dic3d.Faces, vertices_all_frames[0])
                      << "/" << face_count << std::endl;
            std::cout << "  Raw max displacement magnitude: " << raw_max_disp << std::endl;

            bool used_temporal_filtering = false;
            if (config_.step_f_temporal_filtering && vertices_all_frames.size() > 3) {
                std::cout << "\nApplying temporal filtering..." << std::endl;

                const size_t frame_count = vertices_all_frames.size();
                std::vector<std::vector<double>> disp_x(point_count, std::vector<double>(frame_count));
                std::vector<std::vector<double>> disp_y(point_count, std::vector<double>(frame_count));
                std::vector<std::vector<double>> disp_z(point_count, std::vector<double>(frame_count));

                for (size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
                    for (size_t point_index = 0; point_index < point_count; ++point_index) {
                        disp_x[point_index][frame_index] = vertices_all_frames[frame_index][point_index].x() - vertices_ref[point_index].x();
                        disp_y[point_index][frame_index] = vertices_all_frames[frame_index][point_index].y() - vertices_ref[point_index].y();
                        disp_z[point_index][frame_index] = vertices_all_frames[frame_index][point_index].z() - vertices_ref[point_index].z();
                    }
                }

                const double freq_filt = config_.step_f_freq_filt;
                const double freq_acq = config_.vid_sample_freq;
                auto [filt_x, filt_y, filt_z] = filterTime3D(disp_x, disp_y, disp_z, freq_filt, freq_acq);

                VertexSeries filtered = vertices_all_frames;
                for (size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
                    for (size_t point_index = 0; point_index < point_count; ++point_index) {
                        filtered[frame_index][point_index].x() = vertices_ref[point_index].x() + filt_x[point_index][frame_index];
                        filtered[frame_index][point_index].y() = vertices_ref[point_index].y() + filt_y[point_index][frame_index];
                        filtered[frame_index][point_index].z() = vertices_ref[point_index].z() + filt_z[point_index][frame_index];
                    }
                }

                const size_t filtered_any_nan_points = countPointsWithAnyNan(filtered);
                const auto filtered_nan_counts = countNanPoints(filtered);
                const size_t filtered_valid_faces = countValidFaces(dic3d.Faces, filtered[0]);
                const double filtered_max_disp = computeMaxDisplacement(filtered, vertices_ref);
                const bool filtered_exploded =
                    !std::isfinite(filtered_max_disp) ||
                    filtered_max_disp > std::max(1.0, raw_max_disp) * 100.0;

                std::cout << "  Filtered points with any NaN over time: " << filtered_any_nan_points
                          << "/" << point_count << std::endl;
                if (!filtered_nan_counts.empty()) {
                    std::cout << "  Filtered NaN points in frame 0: " << filtered_nan_counts[0]
                              << "/" << point_count << std::endl;
                }
                std::cout << "  Filtered valid faces in frame 0: "
                          << filtered_valid_faces << "/" << face_count << std::endl;
                std::cout << "  Filtered max displacement magnitude: " << filtered_max_disp << std::endl;

                if (filtered_valid_faces == 0 || filtered_valid_faces * 20 < face_count || filtered_exploded) {
                    std::cout << "  Warning: Temporal filtering produced unusable geometry; keeping unfiltered geometry for Step F" << std::endl;
                } else {
                    vertices_all_frames = std::move(filtered);
                    for (size_t point_index = 0; point_index < point_count; ++point_index) {
                        vertices_ref[point_index] = vertices_all_frames[0][point_index];
                    }
                    used_temporal_filtering = true;
                    std::cout << "  \u2713 Temporal filtering applied (freqFilt=" << freq_filt
                              << " Hz, freqAcq=" << freq_acq << " Hz)" << std::endl;
                }
            } else if (config_.step_f_temporal_filtering) {
                std::cout << "\nSkipping temporal filtering (too few frames: "
                          << vertices_all_frames.size() << ")" << std::endl;
            } else {
                std::cout << "\nTemporal filtering disabled (step_f_temporal_filtering=false)" << std::endl;
            }

            std::cout << "\nUpdating Points3D with filtered data..." << std::endl;
            rebuildPoints3D(dic3d, vertices_all_frames);
            if (used_temporal_filtering) {
                std::cout << "  \u2713 Points3D updated with filtered data" << std::endl;
            } else {
                std::cout << "  \u2713 Points3D kept from unfiltered geometry" << std::endl;
            }

            std::cout << "\nRecomputing displacement after filtering..." << std::endl;
            recomputeDisplacement(dic3d, vertices_all_frames, vertices_ref);
            std::cout << "  \u2713 Displacement recomputed for " << vertices_all_frames.size() << " frames" << std::endl;

            std::cout << "\nRecomputing face centroids after filtering..." << std::endl;
            recomputeFaceCentroids(dic3d, vertices_all_frames);
            std::cout << "  \u2713 Face centroids recomputed for " << vertices_all_frames.size() << " frames" << std::endl;

            std::cout << "\nRecomputing face correlation after filtering..." << std::endl;
            recomputeFaceCorrelation(dic3d, vertices_all_frames);

            std::vector<Utils::RigidTransform> rbm_transforms;
            VertexSeries vertices_all_frames_arbm;
            if (config_.step_f_compute_rbm) {
                std::cout << "\nComputing rigid body motion (RBM) transformations..." << std::endl;
                rbm_transforms.resize(vertices_all_frames.size());
                vertices_all_frames_arbm.reserve(vertices_all_frames.size());

                for (size_t frame_index = 0; frame_index < vertices_all_frames.size(); ++frame_index) {
                    Utils::RigidTransform transform;
                    const bool success = Utils::computeRigidTransform(vertices_all_frames[frame_index], vertices_ref, transform);
                    rbm_transforms[frame_index] = transform;
                    if (success) {
                        vertices_all_frames_arbm.push_back(Utils::applyRigidTransform(vertices_all_frames[frame_index], transform));
                    } else {
                        std::cerr << "  Warning: RBM computation failed for frame " << frame_index << std::endl;
                        vertices_all_frames_arbm.push_back(vertices_all_frames[frame_index]);
                    }
                }
                std::cout << "  \u2713 RBM transformations computed for " << vertices_all_frames.size() << " frames" << std::endl;
            } else {
                std::cout << "\nRBM/ARBM computation disabled (step_f_compute_rbm=false)" << std::endl;
            }

            std::cout << "\nComputing 3D surface deformation..." << std::endl;
            std::cout << "  Method: Triangular Cosserat Point Elements (TCPE)" << std::endl;
            std::cout << "  Deformation type: Cumulative (reference = frame 1)" << std::endl;

            FrameDeformationResult deform_result =
                computeTriSurfaceDeformation(dic3d.Faces, vertices_ref, vertices_all_frames, true);
            std::cout << "  \u2713 Deformation computation complete" << std::endl;

            for (size_t frame_index = 0; frame_index < std::min(deform_result.n_frames, size_t(3)); ++frame_index) {
                const auto& frame = deform_result.frames[frame_index];
                size_t nan_f = 0;
                size_t nan_e = 0;
                size_t nan_j = 0;
                size_t inf_count = 0;
                size_t large_count = 0;
                for (size_t i = 0; i < frame.Fmat.size(); ++i) {
                    if (frame.Fmat[i].array().isNaN().any()) {
                        nan_f++;
                    }
                    if (frame.Emat.size() > i && frame.Emat[i].array().isNaN().any()) {
                        nan_e++;
                    }
                    if (frame.J.size() > i && std::isnan(frame.J[i])) {
                        nan_j++;
                    }
                    if (frame.J.size() > i && std::isinf(frame.J[i])) {
                        inf_count++;
                    }
                    if (frame.Emgn.size() > i && std::isfinite(frame.Emgn[i]) && std::abs(frame.Emgn[i]) > 1.0) {
                        large_count++;
                    }
                }
                std::cout << "  [diag] Deform frame " << frame_index << ": "
                          << "NaN_F=" << nan_f << " NaN_E=" << nan_e << " NaN_J=" << nan_j
                          << " Inf=" << inf_count << " |Emgn|>1=" << large_count
                          << " / " << frame.Fmat.size() << " faces" << std::endl;
            }

            FrameDeformationResult deform_result_arbm;
            if (config_.step_f_compute_rbm) {
                std::cout << "\nComputing 3D surface deformation (after RBM removal)..." << std::endl;
                deform_result_arbm =
                    computeTriSurfaceDeformation(dic3d.Faces, vertices_all_frames_arbm[0], vertices_all_frames_arbm, true);
                std::cout << "  \u2713 Deformation computation complete (ARBM)" << std::endl;
            }

            std::cout << "\nBuilding DIC3DPPresults structure..." << std::endl;
            DIC3DPPresults ppresults;
            ppresults.deform_full = deform_result;

            std::cout << "\nComputing face isotropy index..." << std::endl;
            ppresults.FaceIsoInd.resize(vertices_all_frames.size());
            for (size_t frame_index = 0; frame_index < vertices_all_frames.size(); ++frame_index) {
                ppresults.FaceIsoInd[frame_index] = computeFaceIsotropyIndex(dic3d.Faces, vertices_all_frames[frame_index]);
            }
            std::cout << "  \u2713 Face isotropy index computed for " << vertices_all_frames.size() << " frames" << std::endl;

            copyCombinedFields(ppresults, dic3d);
            ppresults.deftype = "cum";
            ppresults.n_frames = deform_result.n_frames;

            populateDeformData(ppresults.Deform, deform_result, face_count);
            std::cout << "  Populated deformation data for " << ppresults.n_frames << " frames" << std::endl;

            if (config_.step_f_compute_rbm) {
                std::cout << "  Populating ARBM deformation data..." << std::endl;
                populateDeformData(ppresults.Deform_ARBM, deform_result_arbm, face_count);
                populateRbmData(ppresults, rbm_transforms, vertices_all_frames_arbm);
                std::cout << "  \u2713 Populated all deformation data (with RBM and ARBM)" << std::endl;
            }

            auto pp_serializer = DataSerializer::create(config_.data_format);
            const std::string pp_out =
                Utils::buildDic3DPPresultsFilePath(output_dir, config_.num_pair, config_.fileversion, pp_serializer->extension());

            if (std::filesystem::exists(pp_out)) {
                std::cout << "\nCheckpoint found: " << pp_out << " (skipping)" << std::endl;
            } else {
                std::cout << "\nWriting DIC3DPPresults to: " << pp_out << std::endl;
                if (!pp_serializer->saveDIC3DPPresults(pp_out, ppresults)) {
                    std::cerr << "ERROR: Failed to write DIC3DPPresults" << std::endl;
                    return false;
                }
                std::cout << "\u2713 Saved DIC3DPPresults: " << pp_out << std::endl;
            }

            exportVisualizationIfEnabled(config_, ppresults, output_dir);

            std::cout << "\n=== Step F Complete ===" << std::endl;
            std::cout << "\u2713 3D deformation and strain analysis finished for trial " << trial << std::endl;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "ERROR in Deformation/Strain Analysis: " << e.what() << std::endl;
        return false;
    }
}

} // namespace cppxdic::pipeline
