/**
 * Utility functions implementation for CPPXDIC
 */

#include "utils.h"
#include "logging.h"
#include <iostream>
#include <filesystem>
#include <regex>
#include <sstream>
#include <iomanip>
#include <fstream>
// Video IO
#include <opencv2/opencv.hpp>
// MAT file IO
#include <matio.h>
// JSON
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <algorithm>

bool Utils::dicCheck(const Config& config) {
    LOG_INFO << "Checking data and protocol...";

    bool success = true;

    // Check parallel processing requirements
    if (config.parallel_processing) {
        LOG_INFO << "Parallel processing is enabled";

        if (!checkROIReferences(config)) {
            success = false;
        }

        if (!checkSeedReferences(config)) {
            success = false;
        }
    }

    // Check protocol files
    if (!checkProtocolFiles(config)) {
        success = false;
    }

    // Check calibration files
    if (!checkCalibrationFiles(config)) {
        success = false;
    }

    if (success) {
        LOG_INFO << "...done Checking.";
    }

    return success;
}

bool Utils::checkCalibrationFiles(const Config& config) {
    try {
        std::string calib_dir = Utils::buildCalibDir(config);
        if (!std::filesystem::exists(calib_dir)) {
            LOG_ERROR << "Calibration directory not found: " << calib_dir;
            return false;
        }

        // Check for all camera pairs based on config.num_pair
        std::vector<bool> cam_found(config.num_pair * 2, false);

        for (const auto& entry : std::filesystem::directory_iterator(calib_dir)) {
            if (!entry.is_regular_file()) continue;
            auto name = entry.path().filename().string();
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            // Check for each camera (1 through num_pair*2)
            for (int cam_id = 1; cam_id <= config.num_pair * 2; ++cam_id) {
                std::string cam_str = "cam_" + std::to_string(cam_id);
                std::string camera_str = "camera" + std::to_string(cam_id);
                if (lower.find(cam_str) != std::string::npos ||
                    lower.find(camera_str) != std::string::npos) {
                    cam_found[cam_id - 1] = true;
                }
            }
        }

        // Check if all cameras were found
        bool all_found = true;
        for (int cam_id = 1; cam_id <= config.num_pair * 2; ++cam_id) {
            if (!cam_found[cam_id - 1]) {
                LOG_ERROR << "Camera " << cam_id << " calibration file missing";
                all_found = false;
            }
        }

        if (!all_found) {
            LOG_ERROR << "Some calibration files missing in " << calib_dir;
            return false;
        }

        LOG_INFO << "Calibration files found for all " << (config.num_pair * 2) << " cameras in "
                 << calib_dir;
        return true;
    } catch (...) {
        LOG_ERROR << "Error while checking calibration files";
        return false;
    }
}

bool Utils::checkROIReferences(const Config& config) {
    LOG_INFO << "Checking ROI References...";

    for (int pair_i = 1; pair_i <= config.num_pair; ++pair_i) {
        std::ostringstream ref_trial_id_oss;
        ref_trial_id_oss << std::setfill('0') << std::setw(3) << config.ref_trial_id;

        std::string roifile =
            Utils::likeRoiPath(Utils::buildOutputUntilMaterialDir(config), ref_trial_id_oss.str(),
                               config.phase_id, pair_i, ".mat");

        if (!fileExists(roifile)) {
            LOG_ERROR << "This file " << roifile
                      << " must exist to be able to run DIC analysis in parallel."
                      << " This file comes from Drawing Reference ROI process."
                      << " ROI file for the stereopair " << pair_i << " from the Reference Trial "
                      << config.ref_trial_id << " for the subject " << config.subject_id
                      << " not found.";
            return false;
        }
    }

    return true;
}

bool Utils::checkSeedReferences(const Config& config) {
    LOG_INFO << "Checking SEED References...";

    for (int pair_i = 1; pair_i <= config.num_pair; ++pair_i) {
        std::ostringstream ref_trial_id_oss;
        ref_trial_id_oss << std::setfill('0') << std::setw(3) << config.ref_trial_id;

        std::string seedfile =
            Utils::likeSeedPath(Utils::buildOutputUntilMaterialDir(config), ref_trial_id_oss.str(),
                                config.phase_id, pair_i, ".mat");

        if (!fileExists(seedfile)) {
            LOG_ERROR << "This file " << seedfile
                      << " must exist to be able to run DIC analysis in parallel."
                      << " This file comes from Selecting the seed of ROI process."
                      << " SEED file for the stereopair " << pair_i << " from the Reference Trial "
                      << config.ref_trial_id << " for the subject " << config.subject_id
                      << " not found.";
            return false;
        }
    }

    return true;
}

bool Utils::checkProtocolFiles(const Config& config) {
    std::string protocol_dir = Utils::buildProtocolDir(config, true, true, true, true);

    auto protocol_files = findFiles(protocol_dir, "*.mat");

    if (protocol_files.empty()) {
        // The protocol .mat drives (a) automatic reference-trial selection and
        // (b) the protocol-derived frame window. Both have explicit config
        // substitutes (ref_trial_id > 0 and idx_frame_start/end), so a missing
        // protocol is only fatal when the reference trial must be inferred.
        if (config.ref_trial_id > 0) {
            LOG_WARN << "No protocol .mat file in " << protocol_dir
                     << " — reference trial comes from ref_trial_id=" << config.ref_trial_id
                     << " and the frame window from idx_frame_start/end ("
                     << config.idx_frame_start << ".." << config.idx_frame_end << ").";
            return true;
        }
        LOG_ERROR << "Protocol files in " << protocol_dir
                  << " must exist to be able to run DIC analysis (needed to pick the reference "
                     "trial; set ref_trial_id / --reftrial to run without one). Protocol not found.";
        return false;
    }

    return true;
}

bool Utils::fileExists(const std::string& path) {
    return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

bool Utils::directoryExists(const std::string& path) {
    return std::filesystem::exists(path) && std::filesystem::is_directory(path);
}

std::vector<std::string> Utils::findFiles(const std::string& directory,
                                          const std::string& pattern) {
    std::vector<std::string> files;

    if (!directoryExists(directory)) {
        return files;
    }

    try {
        // Convert glob pattern to regex
        std::string regex_pattern = pattern;
        std::replace(regex_pattern.begin(), regex_pattern.end(), '*', '.');
        regex_pattern = ".*" + regex_pattern + ".*";

        std::regex file_regex(regex_pattern);

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (std::regex_match(filename, file_regex)) {
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "Error searching for files: " << e.what();
    }

    return files;
}

std::vector<std::string> Utils::split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

// Process-wide stereopair table. Defaults to the historical 4-camera rig
// (pair1 = cams 1,2 ; pair2 = cams 4,3) so callers that never register a
// table (tests, tools) keep the legacy behaviour. main.cpp / DicAnalysis
// register Config::camera_pairs so that the dic_params.txt `camera_pairs`
// key is honoured everywhere (Step D import/tracking, Step E DLT lookup,
// checkpoint paths...). Before this registry existed the config key was parsed
// but silently ignored: every pair >= 3 was mapped to cameras (1,2).
static const std::vector<std::pair<int, int>> kLegacyCameraPairs = {{1, 2}, {4, 3}};
static std::vector<std::pair<int, int>> g_camera_pairs = kLegacyCameraPairs;

void Utils::setCameraPairs(const std::vector<std::pair<int, int>>& camera_pairs) {
    g_camera_pairs = camera_pairs.empty() ? kLegacyCameraPairs : camera_pairs;
}

const std::vector<std::pair<int, int>>& Utils::cameraPairs() {
    return g_camera_pairs;
}

void Utils::getCamerasForPair(int stereopair, int& cam_first, int& cam_second) {
    getCamerasForPair(stereopair, g_camera_pairs, cam_first, cam_second);
}

void Utils::getCamerasForPair(int stereopair, const std::vector<std::pair<int, int>>& camera_pairs,
                              int& cam_first, int& cam_second) {
    if (stereopair < 1) {
        throw std::runtime_error("Invalid stereopair value: must be >= 1");
    }
    int idx = stereopair - 1; // 0-indexed
    if (idx < static_cast<int>(camera_pairs.size())) {
        cam_first = camera_pairs[idx].first;
        cam_second = camera_pairs[idx].second;
    } else {
        // Beyond the configured table: generic mapping pair N -> cameras (2N-1, 2N).
        cam_first = 2 * stereopair - 1;
        cam_second = 2 * stereopair;
    }
}

static bool ensure_dir(const std::string& path) {
    try {
        if (!std::filesystem::exists(path)) {
            return std::filesystem::create_directories(path);
        }
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// Video ingestion (Section 3a)
// ----------------------------------------------------------------------------
// CPPXDIC ingests one .mp4 file per camera (port of MATLAB import_raw_vid.m).
//
// Expected container / codec:
//   - Container : MP4 (.mp4). Files are matched by the regex naming convention
//                 <subject>_<material>_speckles_<trial:3 digits>_*_cam_<id>.mp4
//                 e.g. S09_coating_speckles_007_545_050_trial005_cam_1.mp4
//   - Codec     : any codec OpenCV's VideoCapture can decode via the system FFmpeg
//                 backend (H.264/AVC is the tested codec). If VideoCapture cannot
//                 open the file, importRawVid logs an error and returns false.
//   - Pixels    : speckle videos are stored as RGB; to match MATLAB's iloc(:,:,1)
//                 we extract ONLY the Red channel (BGR index 2 in OpenCV) as the
//                 grayscale frame. Single-channel inputs are used as-is.
//   - Frames    : 1-based frame indices (MATLAB convention); frameStart is clamped
//                 to >= 1 and frameEnd to <= min(frame_count of both cameras).
//
// Frames are decoded to per-camera PNGs under
//   <dic_path>/<subject>/<material>/tmp_frames/T<trial>/pair<n>/cam<id>/ ,
// which the DIC engine then consumes.
// ============================================================================
static std::string find_video_file(const std::string& video_dir, const std::string& subject,
                                   const std::string& material, const std::string& trialname,
                                   int cam_id) {
    if (!Utils::directoryExists(video_dir)) return "";
    // Strict pattern (historical naming):
    //   <subject>_<material>_speckles_<trialname with 3 digits>_.*_cam_<cam_id>.mp4
    //   e.g. S09_coating_speckles_007_545_050_trial005_cam_1.mp4
    // Relaxed fallback (other acquisition rigs, e.g. MNG "<subj>_<finger>_<block>_<trial>_..."):
    //   any file carrying "_<trialname>_" and ending in "_cam_<cam_id>.mp4" — the camera id
    //   is anchored so cam_1 never matches cam_10 / cam_1_0.
    const std::regex strict(subject + "_" + material + "_speckles_" + trialname + "_.*_cam_" +
                            std::to_string(cam_id) + "\\.mp4$");
    const std::regex relaxed(".*_" + trialname + "_.*_cam_" + std::to_string(cam_id) + "\\.mp4$");

    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(video_dir)) {
        if (!entry.is_regular_file()) continue;
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());  // deterministic choice when several match

    for (const std::regex* pat : {&strict, &relaxed}) {
        for (const auto& name : names) {
            if (std::regex_match(name, *pat)) {
                if (pat == &relaxed) {
                    LOG_WARN << "Video for trial " << trialname << " cam " << cam_id
                             << " matched by the relaxed pattern (no '" << subject << "_" << material
                             << "_speckles_' prefix): " << name;
                }
                return (std::filesystem::path(video_dir) / name).string();
            }
        }
    }
    return "";
}

std::string padNumberWithZeros(int num, int n) {
    std::ostringstream oss;
    oss << std::setw(n) << std::setfill('0') << num;
    return oss.str();
}

bool Utils::importRawVid(const Config& config, int trial, int stereopair, int frameStart,
                         int frameEnd, int frameJump, std::vector<std::string>& cam1Frames,
                         std::vector<std::string>& cam2Frames, int maxFrames) {
    try {
        int cam_first = 0, cam_second = 0;
        getCamerasForPair(stereopair, cam_first, cam_second);

        std::string trialname = padNumberWithZeros(trial, 3);
        std::string video_dir = Utils::buildVideoDir(config, true, true, true, true);

        std::string vid1 =
            find_video_file(video_dir, config.subject_id, config.material, trialname, cam_first);
        std::string vid2 =
            find_video_file(video_dir, config.subject_id, config.material, trialname, cam_second);
        if (vid1.empty() || vid2.empty()) {
            LOG_ERROR << "Video files not found for trial=" << trial << " pair=" << stereopair;
            return false;
        }

        cv::VideoCapture cap1(vid1);
        cv::VideoCapture cap2(vid2);
        if (!cap1.isOpened() || !cap2.isOpened()) {
            LOG_ERROR << "Failed to open video files: " << vid1 << " or " << vid2;
            return false;
        }

        LOG_INFO << "Video loaded successfully.";
        LOG_INFO << "Video 1: " << vid1;
        LOG_INFO << "Video 2: " << vid2;

        int total1 = static_cast<int>(cap1.get(cv::CAP_PROP_FRAME_COUNT));
        int total2 = static_cast<int>(cap2.get(cv::CAP_PROP_FRAME_COUNT));
        if (frameEnd <= 0) frameEnd = std::min(total1, total2);
        frameStart = std::max(1, frameStart);
        frameEnd = std::min(frameEnd, std::min(total1, total2));
        if (frameJump <= 0) frameJump = 1;

        // Limit to the first maxFrames selected frames when requested (e.g. only
        // the reference frame is needed), avoiding a full second video decode.
        if (maxFrames > 0) {
            int limitedEnd = frameStart + (maxFrames - 1) * frameJump;
            frameEnd = std::min(frameEnd, limitedEnd);
        }

        // Output directory
        std::ostringstream odir;
        odir << config.dic_path << "/" << config.subject_id << "/" << config.material
             << "/tmp_frames/T" << trial << "/pair" << stereopair << "/";
        std::string base_dir = odir.str();
        std::string cam1_dir = base_dir + "cam" + std::to_string(cam_first);
        std::string cam2_dir = base_dir + "cam" + std::to_string(cam_second);
        if (!ensure_dir(cam1_dir) || !ensure_dir(cam2_dir)) {
            LOG_ERROR << "Failed to create frame directories";
            return false;
        }

        // Extract frames - MATLAB's readvid uses iloc(:,:,1) which takes only the first channel (R)
        // OpenCV reads as BGR, so we need to extract the R channel (index 2 in BGR)
        //
        // Performance: read sequentially instead of seeking before every frame.
        // On the FFmpeg backend (Linux) cap.set(CAP_PROP_POS_FRAMES, n) seeks to
        // the previous keyframe and re-decodes forward on every call, turning a
        // linear read into ~O(N * GOP) work and re-decoding the same frames many
        // times. We seek once to the start, then read forward; when frameJump > 1
        // the skipped frames are grabbed (decoded but not converted to Mat).
        if (frameStart > 1) {
            cap1.set(cv::CAP_PROP_POS_FRAMES, frameStart - 1);
            cap2.set(cv::CAP_PROP_POS_FRAMES, frameStart - 1);
        }

        int cur = frameStart; // 1-based index of the frame the next read()/grab() yields
        bool eof = false;
        for (int f = frameStart; f <= frameEnd && !eof; f += frameJump) {
            // Skip intermediate frames (frameJump > 1) without decoding to Mat.
            while (cur < f) {
                if (!cap1.grab() || !cap2.grab()) { eof = true; break; }
                ++cur;
            }
            if (eof) break;

            cv::Mat im1, im2;
            if (!cap1.read(im1) || !cap2.read(im2)) break;
            ++cur;

            // Extract R channel to match MATLAB's iloc(:,:,1)
            // OpenCV reads as BGR, so R is at index 2
            cv::Mat gray1, gray2;
            if (im1.channels() == 3) {
                std::vector<cv::Mat> channels1;
                cv::split(im1, channels1);
                gray1 = channels1[2]; // R channel (BGR -> index 2)
            } else {
                gray1 = im1;
            }

            if (im2.channels() == 3) {
                std::vector<cv::Mat> channels2;
                cv::split(im2, channels2);
                gray2 = channels2[2]; // R channel (BGR -> index 2)
            } else {
                gray2 = im2;
            }

            std::ostringstream f1, f2;
            f1 << cam1_dir << "/frame_" << std::setw(6) << std::setfill('0') << f << ".png";
            f2 << cam2_dir << "/frame_" << std::setw(6) << std::setfill('0') << f << ".png";
            cv::imwrite(f1.str(), gray1);
            cv::imwrite(f2.str(), gray2);
            cam1Frames.push_back(f1.str());
            cam2Frames.push_back(f2.str());
        }

        return !cam1Frames.empty() && !cam2Frames.empty();
    } catch (const std::exception& e) {
        LOG_ERROR << "importRawVid error: " << e.what();
        return false;
    }
}

bool Utils::importVid(const Config& config, int trial, int stereopair,
                      std::vector<std::string>& cam1Frames, std::vector<std::string>& cam2Frames,
                      int maxFrames) {
    auto calc_ranges = [](const std::string& phase, int nLoad, int nSlide, int nRelax, int& s,
                          int& e) {
        if (phase == "loading") {
            s = 1;
            e = s + nLoad - 1;
            return true;
        }
        if (phase == "slide1") {
            s = nLoad + 1;
            e = s + nSlide - 1;
            return true;
        }
        if (phase == "relax1") {
            s = nLoad + nSlide + 1;
            e = s + nRelax - 1;
            return true;
        }
        if (phase == "slide2") {
            s = nLoad + nSlide + nRelax + 1;
            e = s + nSlide - 1;
            return true;
        }
        if (phase == "relax2") {
            s = nLoad + 2 * nSlide + nRelax + 1;
            e = s + nRelax - 1;
            return true;
        }
        if (phase == "all") {
            s = 1;
            e = nLoad + 2 * nSlide + 2 * nRelax;
            return true;
        }
        return false;
    };

    int frameStart = config.idx_frame_start;
    int frameEnd = config.idx_frame_end;
    int frameJump = config.frame_jump;

    // Read protocol to compute ranges if available. With force_frame_window=true
    // the config window is authoritative and the protocol is not consulted.
    try {
        std::string protocol_dir = Utils::buildProtocolDir(config, true, true, true, true);

        auto protos = config.force_frame_window ? std::vector<std::string>{}
                                                : Utils::findFiles(protocol_dir, "*.mat");
        if (config.force_frame_window) {
            LOG_INFO << "force_frame_window=true: using config frames " << frameStart << ".."
                     << (frameEnd > 0 ? std::to_string(frameEnd) : std::string("end"))
                     << " (jump " << frameJump << "), protocol window ignored";
        }
        if (!protos.empty()) {
            std::string proto_file = protos.front();
            mat_t* matfp = Mat_Open(proto_file.c_str(), MAT_ACC_RDONLY);
            if (matfp) {
                matvar_t* cond = Mat_VarRead(matfp, "cond");
                if (cond && cond->class_type == MAT_C_STRUCT) {
                    matvar_t* table = Mat_VarGetStructFieldByName(cond, "table", 0);
                    matvar_t* dur = Mat_VarGetStructFieldByName(cond, "dur", 0);
                    if (table && table->class_type == MAT_C_CELL && dur &&
                        (dur->class_type == MAT_C_DOUBLE || dur->class_type == MAT_C_SINGLE)) {
                        int nLoad = 0, nSlide = 0, nRelax = 0;
                        if (dur->data && dur->nbytes >= 5 * dur->data_size) {
                            double d3 = 0.0, d5 = 0.0;
                            if (dur->class_type == MAT_C_DOUBLE) {
                                const double* dd = static_cast<const double*>(dur->data);
                                d3 = dd[2];
                                d5 = dd[4];
                            } else {
                                const float* ff = static_cast<const float*>(dur->data);
                                d3 = ff[2];
                                d5 = ff[4];
                            }
                            int fps = 50;
                            nLoad = static_cast<int>(std::round(d3 / 1e3 * fps));
                            // Slide duration depends on dst/spd
                            size_t ntrial = table->dims[0];
                            size_t ncond = table->dims[1];
                            size_t i = static_cast<size_t>(std::max(1, trial) - 1);
                            auto cell_at = [&](size_t ii, size_t jj) -> matvar_t* {
                                size_t idx = ii + jj * ntrial;
                                return static_cast<matvar_t**>(table->data)[idx];
                            };
                            double dst = 0.0, spd = 0.0;
                            if (ncond >= 5) {
                                matvar_t* c_dst = cell_at(i, 4);
                                matvar_t* c_spd = cell_at(i, 3);
                                if (c_dst && c_dst->data) {
                                    if (c_dst->class_type == MAT_C_DOUBLE)
                                        dst = static_cast<const double*>(c_dst->data)[0];
                                    else if (c_dst->class_type == MAT_C_SINGLE)
                                        dst = static_cast<const float*>(c_dst->data)[0];
                                }
                                if (c_spd && c_spd->data) {
                                    if (c_spd->class_type == MAT_C_DOUBLE)
                                        spd = static_cast<const double*>(c_spd->data)[0];
                                    else if (c_spd->class_type == MAT_C_SINGLE)
                                        spd = static_cast<const float*>(c_spd->data)[0];
                                }
                            }
                            if (spd > 0.0)
                                nSlide = static_cast<int>(std::round(dst / spd * fps));
                            else
                                nSlide = 0;
                            nRelax = static_cast<int>(std::round(d5 / 1e3 * fps));
                            int s = frameStart, e = frameEnd;
                            if (calc_ranges(config.phase_id, nLoad, nSlide, nRelax, s, e)) {
                                frameStart = s;
                                frameEnd = e;
                            }
                        }
                    }
                }
                if (cond) Mat_VarFree(cond);
                Mat_Close(matfp);
            }
        }
    } catch (...) {
        // Fallback to config frames silently
    }

    return importRawVid(config, trial, stereopair, frameStart, frameEnd, frameJump, cam1Frames,
                        cam2Frames, maxFrames);
}

bool Utils::loadROIFromMat(const Config& config, int trial, int stereopair,
                           std::string& roiJsonPath, std::string& roiMaskImagePath) {
    try {
        std::ostringstream matname;
        matname << config.dic_path << "/" << config.subject_id << "/" << config.material
                << "/REF_MASK_" << std::setw(3) << std::setfill('0') << config.ref_trial_id << "_"
                << config.phase_id << "_pair" << stereopair << ".mat";
        std::string mat_path = matname.str();
        if (!fileExists(mat_path)) {
            roiJsonPath.clear();
            roiMaskImagePath.clear();
            return false;
        }
        cv::Mat maskImg;
        mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
        if (matfp) {
            // Read exact Matlab variable name 'refmask'
            matvar_t* mv = Mat_VarRead(matfp, "refmask");
            if (mv && mv->rank == 2 && mv->data && mv->dims[0] > 0 && mv->dims[1] > 0) {
                size_t h = mv->dims[0], w = mv->dims[1];
                cv::Mat m(h, w, CV_8UC1, cv::Scalar(0));
                if (mv->class_type == MAT_C_DOUBLE) {
                    const double* d = static_cast<const double*>(mv->data);
                    for (size_t y = 0; y < h; ++y)
                        for (size_t x = 0; x < w; ++x)
                            m.at<uchar>(y, x) = d[y + x * h] > 0.5 ? 255 : 0;
                } else if (mv->class_type == MAT_C_SINGLE) {
                    const float* d = static_cast<const float*>(mv->data);
                    for (size_t y = 0; y < h; ++y)
                        for (size_t x = 0; x < w; ++x)
                            m.at<uchar>(y, x) = d[y + x * h] > 0.5 ? 255 : 0;
                } else if (mv->class_type == MAT_C_UINT8) {
                    const uint8_t* d = static_cast<const uint8_t*>(mv->data);
                    for (size_t y = 0; y < h; ++y)
                        for (size_t x = 0; x < w; ++x) m.at<uchar>(y, x) = d[y + x * h] ? 255 : 0;
                }
                maskImg = m.clone();
            }
            if (mv) Mat_VarFree(mv);
            Mat_Close(matfp);
        }
        std::ostringstream jsonname;
        jsonname << config.dic_path << "/" << config.subject_id << "/" << config.material
                 << "/REF_MASK_" << config.subject_id << "_" << config.material << "_"
                 << config.phase_id << "_T" << trial << "_pair" << stereopair << ".json";
        roiJsonPath = jsonname.str();
        // If mask found, write mask image alongside JSON
        roiMaskImagePath.clear();
        if (!maskImg.empty()) {
            std::ostringstream pm;
            pm << config.dic_path << "/" << config.subject_id << "/" << config.material
               << "/REF_MASK_" << config.subject_id << "_" << config.material << "_"
               << config.phase_id << "_T" << trial << "_pair" << stereopair << ".png";
            if (ensure_dir(std::filesystem::path(pm.str()).parent_path().string())) {
                cv::imwrite(pm.str(), maskImg);
                roiMaskImagePath = pm.str();
            }
        }
        nlohmann::json j;
        j["trial"] = trial;
        j["pair"] = stereopair;
        j["phase"] = config.phase_id;
        j["source"] = {{"type", "mat"}, {"path", mat_path}};
        j["mask_path"] = roiMaskImagePath;
        std::ofstream ofs(roiJsonPath);
        ofs << j.dump(2) << std::endl;
        return true;
    } catch (...) {
        roiJsonPath.clear();
        roiMaskImagePath.clear();
        return false;
    }
}

bool Utils::loadSeedFromMat(const Config& config, int trial, int stereopair,
                            std::string& seedJsonPath) {
    try {
        std::ostringstream matname;
        matname << config.dic_path << "/" << config.subject_id << "/" << config.material
                << "/REF_SEED_" << std::setw(3) << std::setfill('0') << config.ref_trial_id << "_"
                << config.phase_id << "_pair" << stereopair << ".mat";
        std::string mat_path = matname.str();
        if (!fileExists(mat_path)) {
            seedJsonPath.clear();
            return false;
        }
        std::vector<std::string> varnames;
        std::vector<std::pair<double, double>> seeds;
        double radius = static_cast<double>(config.subregion_radius);
        mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
        if (matfp) {
            // Enumerate variables and collect candidates
            matvar_t* vinfo = nullptr;
            std::vector<std::string> candidates;
            while ((vinfo = Mat_VarReadNextInfo(matfp)) != nullptr) {
                if (vinfo->name) {
                    varnames.emplace_back(vinfo->name);
                    std::string n(vinfo->name);
                    std::string nl = n;
                    std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
                    if (nl.find("seed") != std::string::npos || nl == "xy" || nl == "points") {
                        candidates.push_back(n);
                    } else if (nl == "radius" || nl == "r") {
                        // handled later when reading
                    }
                }
                Mat_VarFree(vinfo);
            }
            Mat_Close(matfp);
            // Reopen to read
            matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
            if (matfp) {
                auto try_read_points = [&](const std::string& name) -> bool {
                    matvar_t* mv = Mat_VarRead(matfp, name.c_str());
                    if (!mv) return false;
                    bool ok = false;
                    if (mv->rank == 2 && mv->data && mv->dims[1] >= 2) {
                        size_t rows = mv->dims[0];
                        size_t cols = mv->dims[1];
                        if (cols >= 2 && rows >= 1) {
                            if (mv->class_type == MAT_C_DOUBLE) {
                                const double* d = static_cast<const double*>(mv->data);
                                for (size_t i = 0; i < rows; i++)
                                    seeds.emplace_back(d[i + 0 * rows], d[i + 1 * rows]);
                                ok = true;
                            } else if (mv->class_type == MAT_C_SINGLE) {
                                const float* d = static_cast<const float*>(mv->data);
                                for (size_t i = 0; i < rows; i++)
                                    seeds.emplace_back(d[i + 0 * rows], d[i + 1 * rows]);
                                ok = true;
                            }
                        }
                    }
                    Mat_VarFree(mv);
                    return ok;
                };
                bool got = false;
                for (const auto& c : candidates) {
                    if (try_read_points(c)) {
                        got = true;
                        break;
                    }
                }
                // Also try common names
                if (!got) {
                    try_read_points("seeds");
                }
                if (!got) {
                    try_read_points("seed");
                }
                if (!got) {
                    try_read_points("xy");
                }
                // Try read radius
                auto try_read_radius = [&](const std::string& name) {
                    matvar_t* mv = Mat_VarRead(matfp, name.c_str());
                    if (!mv) return;
                    if (mv->data) {
                        if (mv->class_type == MAT_C_DOUBLE)
                            radius = static_cast<const double*>(mv->data)[0];
                        else if (mv->class_type == MAT_C_SINGLE)
                            radius = static_cast<const float*>(mv->data)[0];
                        else if (mv->class_type == MAT_C_INT32)
                            radius = static_cast<const int32_t*>(mv->data)[0];
                    }
                    Mat_VarFree(mv);
                };
                try_read_radius("radius");
                try_read_radius("r");
                Mat_Close(matfp);
            }
        }
        std::ostringstream jsonname;
        jsonname << config.dic_path << "/" << config.subject_id << "/" << config.material
                 << "/REF_SEED_" << config.subject_id << "_" << config.material << "_"
                 << config.phase_id << "_T" << trial << "_pair" << stereopair << ".json";
        seedJsonPath = jsonname.str();
        std::ofstream ofs(seedJsonPath);
        ofs << "{\n";
        ofs << "  \"trial\": " << trial << ",\n";
        ofs << "  \"pair\": " << stereopair << ",\n";
        ofs << "  \"phase\": \"" << config.phase_id << "\",\n";
        ofs << "  \"source\": { \"type\": \"mat\", \"path\": \"" << mat_path << "\" },\n";
        ofs << "  \"vars\": [";
        for (size_t i = 0; i < varnames.size(); ++i) {
            ofs << "\"" << varnames[i] << "\"";
            if (i + 1 < varnames.size()) ofs << ", ";
        }
        ofs << "],\n";
        ofs << "  \"radius\": " << radius << ",\n";
        ofs << "  \"seeds\": [";
        for (size_t i = 0; i < seeds.size(); ++i) {
            ofs << "{ \"x\": " << seeds[i].first << ", \"y\": " << seeds[i].second << " }";
            if (i + 1 < seeds.size()) ofs << ", ";
        }
        ofs << "]\n";
        ofs << "}\n";
        return true;
    } catch (...) {
        seedJsonPath.clear();
        return false;
    }
}

// ============================================================================
// Camera Calibration and Distortion Removal
// ============================================================================

bool Utils::loadCameraParameters(const std::string& mat_path, CameraParameters& params) {
    params.is_valid = false;

    if (!fileExists(mat_path)) {
        LOG_ERROR << "Camera parameters file not found: " << mat_path;
        return false;
    }

    try {
        mat_t* mat_file = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
        if (!mat_file) {
            LOG_ERROR << "Failed to open MAT file: " << mat_path;
            return false;
        }

        // Read 'cameraCBparameters' structure
        matvar_t* cb_struct = Mat_VarRead(mat_file, "cameraCBparameters");
        if (!cb_struct || cb_struct->class_type != MAT_C_STRUCT) {
            LOG_ERROR << "Variable 'cameraCBparameters' not found or not a struct";
            Mat_Close(mat_file);
            return false;
        }

        // Get 'cameraParameters' field
        matvar_t* cam_params = Mat_VarGetStructFieldByName(cb_struct, "cameraParameters", 0);
        if (!cam_params || cam_params->class_type != MAT_C_STRUCT) {
            LOG_ERROR << "Field 'cameraParameters' not found or not a struct";
            Mat_VarFree(cb_struct);
            Mat_Close(mat_file);
            return false;
        }

        // Read IntrinsicMatrix (3x3)
        matvar_t* intrinsic = Mat_VarGetStructFieldByName(cam_params, "IntrinsicMatrix", 0);
        if (intrinsic && intrinsic->data && intrinsic->class_type == MAT_C_DOUBLE) {
            double* data = static_cast<double*>(intrinsic->data);
            // MATLAB stores in column-major, OpenCV uses row-major
            // IntrinsicMatrix in MATLAB: [fx 0 0; 0 fy 0; cx cy 1]
            params.camera_matrix = cv::Mat(3, 3, CV_64F);
            params.camera_matrix.at<double>(0, 0) = data[0]; // fx
            params.camera_matrix.at<double>(0, 1) = data[3]; // skew (usually 0)
            params.camera_matrix.at<double>(0, 2) = data[6]; // cx
            params.camera_matrix.at<double>(1, 0) = data[1]; // 0
            params.camera_matrix.at<double>(1, 1) = data[4]; // fy
            params.camera_matrix.at<double>(1, 2) = data[7]; // cy
            params.camera_matrix.at<double>(2, 0) = data[2]; // 0
            params.camera_matrix.at<double>(2, 1) = data[5]; // 0
            params.camera_matrix.at<double>(2, 2) = data[8]; // 1
        } else {
            LOG_ERROR << "IntrinsicMatrix not found or invalid";
            Mat_VarFree(cb_struct);
            Mat_Close(mat_file);
            return false;
        }

        // Read RadialDistortion (1x2 or 1x3)
        matvar_t* radial = Mat_VarGetStructFieldByName(cam_params, "RadialDistortion", 0);
        std::vector<double> dist_coeffs;
        if (radial && radial->data && radial->class_type == MAT_C_DOUBLE) {
            double* data = static_cast<double*>(radial->data);
            size_t n = 1;
            for (int i = 0; i < radial->rank; i++) n *= radial->dims[i];
            for (size_t i = 0; i < n && i < 3; ++i) {
                dist_coeffs.push_back(data[i]);
            }
        }

        // Read TangentialDistortion (1x2)
        matvar_t* tangential = Mat_VarGetStructFieldByName(cam_params, "TangentialDistortion", 0);
        std::vector<double> tan_coeffs;
        if (tangential && tangential->data && tangential->class_type == MAT_C_DOUBLE) {
            double* data = static_cast<double*>(tangential->data);
            size_t n = 1;
            for (int i = 0; i < tangential->rank; i++) n *= tangential->dims[i];
            for (size_t i = 0; i < n && i < 2; ++i) {
                tan_coeffs.push_back(data[i]);
            }
        }

        // OpenCV distortion format: [k1, k2, p1, p2, k3, k4, k5, k6]
        // MATLAB: RadialDistortion=[k1, k2, k3], TangentialDistortion=[p1, p2]
        params.distortion_coeffs = cv::Mat::zeros(1, 5, CV_64F);
        if (dist_coeffs.size() >= 1)
            params.distortion_coeffs.at<double>(0, 0) = dist_coeffs[0]; // k1
        if (dist_coeffs.size() >= 2)
            params.distortion_coeffs.at<double>(0, 1) = dist_coeffs[1];                        // k2
        if (tan_coeffs.size() >= 1) params.distortion_coeffs.at<double>(0, 2) = tan_coeffs[0]; // p1
        if (tan_coeffs.size() >= 2) params.distortion_coeffs.at<double>(0, 3) = tan_coeffs[1]; // p2
        if (dist_coeffs.size() >= 3)
            params.distortion_coeffs.at<double>(0, 4) = dist_coeffs[2]; // k3

        Mat_VarFree(cb_struct);
        Mat_Close(mat_file);

        params.is_valid = true;
        LOG_INFO << "Loaded camera parameters from: " << mat_path;
        LOG_DEBUG << "  Intrinsic matrix: " << params.camera_matrix;
        LOG_DEBUG << "  Distortion coeffs: " << params.distortion_coeffs;

        return true;

    } catch (const std::exception& e) {
        LOG_ERROR << "Exception loading camera parameters: " << e.what();
        return false;
    }
}

void Utils::undistortPoints(const std::vector<cv::Point2d>& points_in,
                            const CameraParameters& params, std::vector<cv::Point2d>& points_out) {
    if (!params.is_valid) {
        LOG_WARN << "Invalid camera parameters, cannot undistort points";
        points_out = points_in;
        return;
    }

    if (points_in.empty()) {
        points_out.clear();
        return;
    }

    // OpenCV's undistortPoints outputs normalized coordinates, we need pixel coordinates
    // Use undistortImagePoints or manual approach

    points_out.resize(points_in.size());

    for (size_t i = 0; i < points_in.size(); ++i) {
        points_out[i] = undistortPoint(points_in[i], params);
    }
}

cv::Point2d Utils::undistortPoint(const cv::Point2d& point_in, const CameraParameters& params) {
    if (!params.is_valid) {
        return point_in;
    }

    // Extract camera parameters
    double fx = params.camera_matrix.at<double>(0, 0);
    double fy = params.camera_matrix.at<double>(1, 1);
    double cx = params.camera_matrix.at<double>(0, 2);
    double cy = params.camera_matrix.at<double>(1, 2);

    double k1 = params.distortion_coeffs.at<double>(0, 0);
    double k2 = params.distortion_coeffs.at<double>(0, 1);
    double p1 = params.distortion_coeffs.at<double>(0, 2);
    double p2 = params.distortion_coeffs.at<double>(0, 3);
    double k3 = params.distortion_coeffs.at<double>(0, 4);

    // Convert to normalized coordinates
    double x = (point_in.x - cx) / fx;
    double y = (point_in.y - cy) / fy;

    // Iterative undistortion (Newton-Raphson)
    double x_u = x;
    double y_u = y;

    for (int iter = 0; iter < 10; ++iter) {
        double r2 = x_u * x_u + y_u * y_u;
        double r4 = r2 * r2;
        double r6 = r4 * r2;

        // Radial distortion
        double radial = 1.0 + k1 * r2 + k2 * r4 + k3 * r6;

        // Tangential distortion
        double dx_tan = 2.0 * p1 * x_u * y_u + p2 * (r2 + 2.0 * x_u * x_u);
        double dy_tan = p1 * (r2 + 2.0 * y_u * y_u) + 2.0 * p2 * x_u * y_u;

        // Distorted coordinates
        double x_d = x_u * radial + dx_tan;
        double y_d = y_u * radial + dy_tan;

        // Update undistorted estimate
        double error_x = x_d - x;
        double error_y = y_d - y;

        if (std::abs(error_x) < 1e-10 && std::abs(error_y) < 1e-10) {
            break;
        }

        // Jacobian-based update (simplified)
        x_u -= error_x * 0.9;
        y_u -= error_y * 0.9;
    }

    // Convert back to pixel coordinates
    double x_out = x_u * fx + cx;
    double y_out = y_u * fy + cy;

    return cv::Point2d(x_out, y_out);
}

// ============================================================================
// DLT11 Calibration (matches MATLAB DLT11Calibration.m)
// ============================================================================

bool Utils::DLT11Calibration(const double* P2, const double* P3, size_t N, std::vector<double>& L) {
    if (N < 6) {
        LOG_ERROR << "DLT11Calibration: need at least 6 point correspondences, got " << N;
        return false;
    }

    // Build the 2N x 11 matrix M and 2N x 1 vector b (P2array)
    // MATLAB:
    //   M(1:2:2*N-1,:) = [P3 ones(N,1) zeros(N,4)        -u.*P3]
    //   M(2:2:2*N,:)   = [zeros(N,4)   P3 ones(N,1)      -v.*P3]
    //   P2array(1:2:2*N-1) = u
    //   P2array(2:2:2*N)   = v
    //   L = M \ P2array
    Eigen::MatrixXd M = Eigen::MatrixXd::Zero(2 * N, 11);
    Eigen::VectorXd b(2 * N);

    for (size_t i = 0; i < N; ++i) {
        double u = P2[i * 2 + 0];
        double v = P2[i * 2 + 1];
        double X = P3[i * 3 + 0];
        double Y = P3[i * 3 + 1];
        double Z = P3[i * 3 + 2];

        // Odd row (2i): [X Y Z 1  0 0 0 0  -u*X -u*Y -u*Z]
        size_t r1 = 2 * i;
        M(r1, 0) = X;
        M(r1, 1) = Y;
        M(r1, 2) = Z;
        M(r1, 3) = 1.0;
        M(r1, 8) = -u * X;
        M(r1, 9) = -u * Y;
        M(r1, 10) = -u * Z;
        b(r1) = u;

        // Even row (2i+1): [0 0 0 0  X Y Z 1  -v*X -v*Y -v*Z]
        size_t r2 = 2 * i + 1;
        M(r2, 4) = X;
        M(r2, 5) = Y;
        M(r2, 6) = Z;
        M(r2, 7) = 1.0;
        M(r2, 8) = -v * X;
        M(r2, 9) = -v * Y;
        M(r2, 10) = -v * Z;
        b(r2) = v;
    }

    // Solve via least squares: L = M \ b
    Eigen::VectorXd result = M.colPivHouseholderQr().solve(b);

    L.resize(11);
    for (int i = 0; i < 11; ++i) {
        L[i] = result(i);
    }

    return true;
}

// ============================================================================
// Rigid Body Motion (RBM) Transformation
// ============================================================================

bool Utils::computeRigidTransform(const std::vector<Eigen::Vector3d>& points_from,
                                  const std::vector<Eigen::Vector3d>& points_to,
                                  RigidTransform& transform) {
    transform.is_valid = false;

    if (points_from.size() != points_to.size()) {
        LOG_ERROR << "Point clouds must have same size for rigid transformation";
        return false;
    }

    if (points_from.size() < 3) {
        LOG_ERROR << "Need at least 3 points for rigid transformation";
        return false;
    }

    // Filter out NaN points (matching MATLAB lines 34-42)
    std::vector<Eigen::Vector3d> from_no_nan, to_no_nan;
    for (size_t i = 0; i < points_from.size(); ++i) {
        if (!points_from[i].hasNaN() && !points_to[i].hasNaN()) {
            from_no_nan.push_back(points_from[i]);
            to_no_nan.push_back(points_to[i]);
        }
    }

    if (from_no_nan.size() < 3) {
        LOG_ERROR << "Too few valid points after removing NaNs (" << from_no_nan.size() << " of "
                  << points_from.size() << " points valid, need at least 3)."
                  << " This usually indicates poor correlation/tracking in the DIC analysis";
        return false;
    }

    // Compute centroids (MATLAB lines 44-45)
    Eigen::Vector3d centroid_from = Eigen::Vector3d::Zero();
    Eigen::Vector3d centroid_to = Eigen::Vector3d::Zero();

    for (const auto& p : from_no_nan) {
        centroid_from += p;
    }
    for (const auto& p : to_no_nan) {
        centroid_to += p;
    }

    centroid_from /= from_no_nan.size();
    centroid_to /= to_no_nan.size();

    // Center point clouds (MATLAB lines 47-48)
    std::vector<Eigen::Vector3d> da, db;
    for (const auto& p : from_no_nan) {
        da.push_back(p - centroid_from);
    }
    for (const auto& p : to_no_nan) {
        db.push_back(p - centroid_to);
    }

    // Compute cross-correlation matrix M = db^T * da (MATLAB line 53)
    Eigen::Matrix3d M = Eigen::Matrix3d::Zero();
    for (size_t i = 0; i < da.size(); ++i) {
        M += db[i] * da[i].transpose();
    }

    // SVD decomposition (MATLAB line 54)
    Eigen::JacobiSVD<Eigen::Matrix3d> svd(M, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::Matrix3d U = svd.matrixU();
    Eigen::Matrix3d V = svd.matrixV();

    // Compute rotation ensuring it's a proper rotation (det = +1) (MATLAB lines 55-56)
    double det_UV = (U * V.transpose()).determinant();
    Eigen::Matrix3d S = Eigen::Matrix3d::Identity();
    S(2, 2) = det_UV; // Set diagonal element to det(U*V') to ensure proper rotation

    transform.R = U * S * V.transpose();

    // Compute translation (MATLAB line 57)
    transform.t = centroid_to - transform.R * centroid_from;

    transform.is_valid = true;

    return true;
}

std::vector<Eigen::Vector3d> Utils::applyRigidTransform(
    const std::vector<Eigen::Vector3d>& points_in, const RigidTransform& transform) {
    std::vector<Eigen::Vector3d> points_out;
    points_out.reserve(points_in.size());

    if (!transform.is_valid) {
        LOG_WARN << "Invalid transform, returning original points";
        return points_in;
    }

    // Apply transformation: p_out = R * p_in + t (MATLAB line 71)
    for (const auto& p : points_in) {
        if (p.hasNaN()) {
            points_out.push_back(p); // Keep NaN points as is
        } else {
            points_out.push_back(transform.R * p + transform.t);
        }
    }

    return points_out;
}

std::string Utils::buildBaseDataPath(const Config& config, bool with_data_or_dic_path,
                                     bool with_rawdata, bool with_speckles) {
    std::string path = "";

    if (with_data_or_dic_path) {
        path += config.data_path;

        if (with_rawdata) {
            path += "/rawdata";
        }

        path += "/" + config.subject_id;

        if (with_speckles) {
            path += "/speckles";
        }
    } else {
        path += config.dic_path;
        path += "/" + config.subject_id;
    }

    return path;
}

std::string Utils::buildOutputUntilTrialDir(const Config& config, int trial) {
    std::ostringstream output_ss;
    output_ss << buildBaseDataPath(config, false, false, false);
    output_ss << "/" << config.material;
    output_ss << "/" << std::setw(3) << std::setfill('0') << trial;
    output_ss << "/";
    return output_ss.str();
}

std::string Utils::buildOutputUntilPhaseDir(const Config& config, int trial) {
    std::ostringstream output_ss;
    output_ss << buildBaseDataPath(config, false, false, false);
    output_ss << "/" << config.material;
    output_ss << "/" << std::setw(3) << std::setfill('0') << trial;
    output_ss << "/" << config.phase_id << "/";
    return output_ss.str();
}

std::string Utils::buildPath(const Config& config, bool with_data_or_dic_path, bool with_rawdata,
                             bool with_speckles, bool with_material, bool with_video,
                             bool with_protocol, bool with_calib) {
    std::string path =
        buildBaseDataPath(config, with_data_or_dic_path, with_rawdata, with_speckles);

    if (with_material) {
        path += "/" + config.material;
    }

    if (with_video) {
        path += "/vid";
    }

    if (with_protocol) {
        path += "/protocol";
    }

    if (with_calib) {
        path += "/calib/" + config.calib_folder_set;
    }

    return path;
}

std::string Utils::buildUntilMaterialDir(const Config& config, bool with_data_or_dic_path,
                                         bool with_rawdata, bool with_speckles) {
    std::string path =
        buildPath(config, with_data_or_dic_path, with_rawdata, with_speckles, true, false, false);

    return path + "/";
}

std::string Utils::buildOutputUntilMaterialDir(const Config& config) {
    return buildUntilMaterialDir(config, false, false, false);
}

std::string Utils::buildProtocolDir(const Config& config, bool with_data_or_dic_path,
                                    bool with_rawdata, bool with_speckles, bool with_material) {
    std::string path = buildPath(config, with_data_or_dic_path, with_rawdata, with_speckles,
                                 with_material, false, true);

    return path + "/";
}

std::string Utils::buildVideoDir(const Config& config, bool with_data_or_dic_path,
                                 bool with_rawdata, bool with_speckles, bool with_material) {
    std::string path = buildPath(config, with_data_or_dic_path, with_rawdata, with_speckles,
                                 with_material, true, false);

    return path + "/";
}

std::string Utils::buildCalibDir(const Config& config) {
    std::string path = buildPath(config, false, false, false, false, false, false, true);

    return path + "/";
}

std::string Utils::buildPath(const cppxdic::BaseParameters& parameters, bool with_material,
                             bool with_trial, bool with_phase, bool with_cache) {
    std::string path = parameters.baseResultPath + "/" + parameters.subject;

    if (with_material) {
        path += "/" + parameters.material;
    }

    if (with_trial) {
        path += "/" + parameters.trial;
    }

    if (with_phase) {
        path += "/" + parameters.phase;
    }

    if (with_cache) {
        path += "/.cache";
    }

    return path;
}

std::string Utils::buildOutputPath(const cppxdic::BaseParameters& parameters) {
    return buildPath(parameters, true, true, true, false);
}

std::string Utils::buildOutputCachePath(const cppxdic::BaseParameters& parameters) {
    return buildPath(parameters, true, true, true, true);
}

std::string Utils::likeRoiOrSeedPath(const std::string& path, const std::string_view& prefix,
                                     const std::string& reftrial, const std::string& phase,
                                     int stereopair, const std::string& extension) {
    std::ostringstream oss;
    oss << path << "/" << prefix << reftrial << "_" << phase << "_pair" << stereopair << extension;

    return oss.str();
}

std::string Utils::likeRoiPath(const std::string& path, const std::string& reftrial,
                               const std::string& phase, int stereopair,
                               const std::string& extension) {
    return likeRoiOrSeedPath(path, "REF_MASK_", reftrial, phase, stereopair, extension);
}

std::string Utils::likeSeedPath(const std::string& path, const std::string& reftrial,
                                const std::string& phase, int stereopair,
                                const std::string& extension) {
    return likeRoiOrSeedPath(path, "REF_SEED_", reftrial, phase, stereopair, extension);
}

std::string Utils::buildRoiOrSeedLikeFilePath(const cppxdic::BaseParameters& parameters,
                                              std::string reftrial, int stereopair,
                                              std::string_view prefix,
                                              const std::string& extension) {
    return likeRoiOrSeedPath(buildPath(parameters, true, false, false, false), prefix, reftrial,
                             parameters.phase, stereopair, extension);
}

std::string Utils::buildRoiFilePath(const cppxdic::BaseParameters& parameters, std::string reftrial,
                                    int stereopair, const std::string& extension) {
    return buildRoiOrSeedLikeFilePath(parameters, reftrial, stereopair, "REF_MASK_", extension);
}

std::string Utils::buildSeedFilePath(const cppxdic::BaseParameters& parameters,
                                     std::string reftrial, int stereopair,
                                     const std::string& extension) {
    return buildRoiOrSeedLikeFilePath(parameters, reftrial, stereopair, "REF_SEED_", extension);
}

std::string Utils::buildMatchingFilePath(const cppxdic::BaseParameters& parameters,
                                         std::string reftrial, int stereopair,
                                         const std::string& extension) {
    return parameters.outputPath + "/MATCHING2" + reftrial + "_pair" + std::to_string(stereopair) +
           extension;
}

std::string Utils::buildDic3DCombinedFilePath(const std::string pathdir, int num_pair,
                                              const std::string& ext) {
    return pathdir + "DIC3Dcombined_" + std::to_string(num_pair) + "Pairs_stitched" + ext;
}

std::string Utils::buildDic3DPPresultsFilePath(const std::string pathdir, int num_pair,
                                               const std::string& fileversion,
                                               const std::string& ext) {
    return pathdir + "DIC3DPPresults_" + std::to_string(num_pair) + "Pairs_cum_" + fileversion +
           ext;
}

std::string Utils::buildVizPath(const std::string pathdir, const std::string filename) {
    return pathdir + "viz/" + filename;
}

std::string Utils::buildDic2DPairResultsFilePath(const std::string pathdir, int cam_1, int cam_2,
                                                 const std::string& extension) {
    return pathdir + "/myDIC2DpairResults_C_" + std::to_string(cam_1) + "_C_" +
           std::to_string(cam_2) + extension;
}

std::string Utils::buildNcorrFilePath(const std::string pathdir, int cam_1, int cam_2,
                                      const std::string& extension) {
    if (cam_2 == -1) {
        return pathdir + "/ncorr" + std::to_string(cam_1) + extension;
    }
    return pathdir + "/ncorr" + std::to_string(cam_1) + std::to_string(cam_2) + extension;
}
