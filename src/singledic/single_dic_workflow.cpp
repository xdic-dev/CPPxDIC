/**
 * @file single_dic_workflow.cpp
 * @brief Implementation of the single-camera 2D DIC step-D pipeline.
 *
 * See include/singledic/single_dic_workflow.h for the MATLAB cross-reference.
 * The flow mirrors process_single_trial_ncorr.m, restricted to ONE camera and
 * ONE tracking pass (no stereo matching-between-cameras, no stepE/stepF 3D).
 *
 * Reused CPPxDIC pieces:
 *   - cppxdic::ImageProcessor : saturate(), applyBandpassFilter(),
 *     computePercentileBoundaries(), normalizeAndClamp()  (satur/bandpass/normalise)
 *   - cppxdic::ROIManager     : loadOrCreateROI(), loadOrCreateSeed(),
 *     findROICenter()                                       (ROI/seed handling)
 *   - cppxdic::BaseParameters : the struct the above helpers consume
 * DIC engine: ncorr::NcorrSession (in-memory, dependency-light).
 */

#include "singledic/single_dic_workflow.h"

#include "image_processor.h"
#include "roi_manager.h"
#include "parameters.h"
#include "logging.h"

#include <ncorr/session.h>
#include <ncorr/frame_reader.h>

#include <matio.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace singledic {

SingleDicWorkflow::SingleDicWorkflow(const SinglediConfig& config) : cfg_(config) {}

// ---------------------------------------------------------------------------
// Frame import — video file or numbered image folder.
// Mirrors readvid(...) in process_single_trial_ncorr.m, honouring the
// [idx_frame_start, idx_frame_end] window (1-based), frame_jump, and the
// backward flip.
// ---------------------------------------------------------------------------
bool SingleDicWorkflow::importFrames(std::vector<cv::Mat>& frames) const {
    frames.clear();

    const std::string trial_dir = cfg_.inputTrialDir();

    // Candidate 1: a video file.  Vik stores each trial as <trial>.mp4; we also
    // accept the trial path itself being a file (any extension OpenCV decodes).
    std::vector<std::string> video_candidates;
    if (fs::exists(trial_dir) && fs::is_regular_file(trial_dir)) {
        video_candidates.push_back(trial_dir);
    }
    for (const char* ext : {".mp4", ".avi", ".mov", ".mkv"}) {
        std::string p = trial_dir + ext;
        if (fs::exists(p)) video_candidates.push_back(p);
    }
    // Also: a video file living *inside* the trial directory.
    if (fs::exists(trial_dir) && fs::is_directory(trial_dir)) {
        for (const auto& e : fs::directory_iterator(trial_dir)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".mp4" || ext == ".avi" || ext == ".mov" || ext == ".mkv")
                video_candidates.push_back(e.path().string());
        }
    }

    const int start = std::max(1, cfg_.idx_frame_start); // 1-based
    const int jump = std::max(1, cfg_.frame_jump);

    auto window_done = [&](int count_kept_input_index, int frame_1based) {
        (void)count_kept_input_index;
        return cfg_.idx_frame_end > 0 && frame_1based > cfg_.idx_frame_end;
    };

    if (!video_candidates.empty()) {
        const std::string& vpath = video_candidates.front();
        cv::VideoCapture cap(vpath);
        if (!cap.isOpened()) {
            LOG_ERROR << "[singledic] Cannot open video: " << vpath;
            return false;
        }
        int frame_idx = 0; // 0-based as read
        cv::Mat bgr;
        while (cap.read(bgr)) {
            ++frame_idx; // now 1-based for this frame
            if (frame_idx < start) continue;
            if (window_done(0, frame_idx)) break;
            if (((frame_idx - start) % jump) != 0) continue;
            cv::Mat gray;
            if (bgr.channels() == 3)
                cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
            else
                gray = bgr.clone();
            if (gray.type() != CV_8UC1) gray.convertTo(gray, CV_8UC1);
            frames.push_back(gray);
        }
        cap.release();
    } else if (fs::exists(trial_dir) && fs::is_directory(trial_dir)) {
        // Candidate 2: numbered image folder (reuse ncorr's natural-sort discovery).
        std::vector<std::string> paths;
        try {
            paths = ncorr::discover_frames(trial_dir, "", "");
        } catch (const std::exception& e) {
            LOG_ERROR << "[singledic] " << e.what();
            return false;
        }
        for (size_t i = 0; i < paths.size(); ++i) {
            int frame_1based = static_cast<int>(i) + 1;
            if (frame_1based < start) continue;
            if (window_done(0, frame_1based)) break;
            if (((frame_1based - start) % jump) != 0) continue;
            cv::Mat gray = cv::imread(paths[i], cv::IMREAD_GRAYSCALE);
            if (gray.empty()) {
                LOG_ERROR << "[singledic] Failed to read frame: " << paths[i];
                return false;
            }
            frames.push_back(gray);
        }
    } else {
        LOG_ERROR << "[singledic] No video or image folder at: " << trial_dir;
        return false;
    }

    if (frames.empty()) {
        LOG_ERROR << "[singledic] No frames imported from " << trial_dir;
        return false;
    }

    // Backward tracking flips the temporal order (Vik: flip(im_raw,3)).
    if (cfg_.tracking_dir == "backward") {
        std::reverse(frames.begin(), frames.end());
    }

    LOG_INFO << "[singledic] Imported " << frames.size() << " frame(s) from " << trial_dir;
    return true;
}

// ---------------------------------------------------------------------------
// Saturation — satur(satur(im,'level',high),'method','low','level',low).
// ---------------------------------------------------------------------------
std::vector<cv::Mat> SingleDicWorkflow::saturate(const std::vector<cv::Mat>& frames) const {
    using cppxdic::ImageProcessor;
    std::vector<cv::Mat> out;
    out.reserve(frames.size());
    for (const auto& f : frames) {
        cv::Mat hi = ImageProcessor::saturate(f, cfg_.limit_grayscale_high, "high");
        cv::Mat lo = ImageProcessor::saturate(hi, cfg_.limit_grayscale_low, "low");
        out.push_back(lo);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Optional bandpass + percentile-normalise (filter_like_ben style):
//   im = bandpassfft(im, low, high);
//   y  = prctile(im(roi),[1 99]); im = clamp((im-y1)/(y2-y1),0,1)
// ---------------------------------------------------------------------------
std::vector<cv::Mat> SingleDicWorkflow::maybeFilter(const std::vector<cv::Mat>& frames,
                                                    const cv::Mat& roi_mask) const {
    if (!cfg_.filter_im_mode) return frames;

    using cppxdic::ImageProcessor;
    const std::vector<int> param = {cfg_.param_filt_low, cfg_.param_filt_high};

    // Derive grayscale bounds from the reference (frame 0) inside the ROI, then
    // reuse them for every frame (matches the single-bound approach in Vik's
    // band-pass video loop).
    cv::Mat ref_bp = ImageProcessor::applyBandpassFilter(frames.front(), param);
    auto bounds = ImageProcessor::computePercentileBoundaries(ref_bp, roi_mask, 1.0, 99.0);

    std::vector<cv::Mat> out;
    out.reserve(frames.size());
    for (const auto& f : frames) {
        cv::Mat bp = ImageProcessor::applyBandpassFilter(f, param);
        out.push_back(ImageProcessor::normalizeAndClamp(bp, bounds));
    }
    return out;
}

// ---------------------------------------------------------------------------
// ROI — load REF_MASK_*.mat if present, else full-frame ROI.
// (Reuses cppxdic::ROIManager::loadOrCreateROI, which is non-blocking.)
// ---------------------------------------------------------------------------
cv::Mat SingleDicWorkflow::loadOrCreateRoi(const cv::Mat& reference) const {
    cppxdic::BaseParameters bp = cfg_.toBaseParameters();
    return cppxdic::ROIManager::loadOrCreateROI(bp, reference);
}

// ---------------------------------------------------------------------------
// Seed — load REF_SEED_*.mat if present, else ROI centre.
// ---------------------------------------------------------------------------
std::vector<int> SingleDicWorkflow::loadOrCreateSeed(const cv::Mat& roi_mask) const {
    cppxdic::BaseParameters bp = cfg_.toBaseParameters();
    cppxdic::SeedPoint sp = cppxdic::ROIManager::loadOrCreateSeed(bp, roi_mask);
    return sp.pw;
}

// ---------------------------------------------------------------------------
// Tracking — one ncorr pass: reference frame (frames[0]) vs every current
// frame, driven through the in-memory NcorrSession.
// ---------------------------------------------------------------------------
bool SingleDicWorkflow::track(const std::vector<cv::Mat>& frames, const cv::Mat& roi_mask,
                              const std::vector<int>& seed_pw, SingleDicResult& out) const {
    if (frames.empty()) return false;

    // Map CPPxDIC Config tunables onto the session config so singledic uses the
    // same DIC parameters as the production engine (step_d radius/spacing).
    ncorr::SessionConfig scfg;
    scfg.subregion_radius = cfg_.base.step_d.radius;
    scfg.scalefactor = cfg_.base.step_d.spacing + 1; // grid factor = spacing+1
    scfg.num_threads = std::max(1, cfg_.base.step_d.total_threads);
    scfg.debug = cfg_.base.debug_mode;

    const cv::Mat& ref = frames.front();

    // Ensure contiguous 8-bit grayscale buffers for ImageBuffer views.
    auto as_buffer = [](const cv::Mat& m) -> cv::Mat {
        cv::Mat g = m;
        if (g.channels() != 1) cv::cvtColor(g, g, cv::COLOR_BGR2GRAY);
        if (g.type() != CV_8UC1) g.convertTo(g, CV_8UC1);
        if (!g.isContinuous()) g = g.clone();
        return g;
    };

    cv::Mat ref8 = as_buffer(ref);
    cv::Mat roi8;
    if (!roi_mask.empty()) {
        roi8 = roi_mask;
        if (roi8.type() != CV_8UC1) roi8.convertTo(roi8, CV_8UC1);
        if (!roi8.isContinuous()) roi8 = roi8.clone();
    }

    ncorr::NcorrSession session(scfg);
    try {
        session.set_reference(ncorr::ImageBuffer(ref8.data, ref8.cols, ref8.rows, 1));
        if (!roi8.empty()) {
            session.set_roi(ncorr::ImageBuffer(roi8.data, roi8.cols, roi8.rows, 1));
        }
    } catch (const std::exception& e) {
        out.message = std::string("set_reference/roi failed: ") + e.what();
        return false;
    }

    out.image_size = ref8.size();
    out.roi_mask = roi8.empty() ? cv::Mat() : roi8.clone();
    out.subset_spacing = cfg_.base.step_d.spacing;
    out.seed_pw = seed_pw;
    out.frames.clear();
    out.frames.reserve(frames.size() - 1);

    // frames[0] is the reference; track frames[1..] against it.
    for (size_t i = 1; i < frames.size(); ++i) {
        cv::Mat def8 = as_buffer(frames[i]);
        ncorr::DICResult r;
        try {
            r = session.process_frame(ncorr::ImageBuffer(def8.data, def8.cols, def8.rows, 1));
        } catch (const std::exception& e) {
            LOG_ERROR << "[singledic] frame " << i << " DIC failed: " << e.what();
        }
        FrameResult fr;
        fr.grid_width = r.width;
        fr.grid_height = r.height;
        fr.u = std::move(r.u);
        fr.v = std::move(r.v);
        fr.corrcoef = std::move(r.corrcoef);
        fr.valid = r.valid;
        out.frames.push_back(std::move(fr));
        if (cfg_.base.debug_mode) {
            LOG_DEBUG << "[singledic] tracked frame " << i << "/" << (frames.size() - 1)
                      << (r.valid ? " ok" : " FAILED");
        }
    }

    out.ok = true;
    out.message = "tracking complete";
    return true;
}

// ---------------------------------------------------------------------------
// CSV writer — feature trajectories (seed + per-grid-point u/v/corr per frame).
// ---------------------------------------------------------------------------
bool SingleDicWorkflow::writeCsv(const SingleDicResult& res) const {
    const std::string path = cfg_.ncorrStem() + ".csv";
    std::ofstream f(path);
    if (!f) {
        LOG_ERROR << "[singledic] cannot write CSV: " << path;
        return false;
    }
    f << "frame,grid_x,grid_y,u_px,v_px,corrcoef\n";
    f.setf(std::ios::fmtflags(0), std::ios::floatfield);
    for (size_t fi = 0; fi < res.frames.size(); ++fi) {
        const FrameResult& fr = res.frames[fi];
        for (int gy = 0; gy < fr.grid_height; ++gy) {
            for (int gx = 0; gx < fr.grid_width; ++gx) {
                size_t idx = static_cast<size_t>(gy) * fr.grid_width + gx;
                if (idx >= fr.u.size()) continue;
                double u = fr.u[idx];
                if (std::isnan(u)) continue; // outside ROI
                double v = (idx < fr.v.size()) ? fr.v[idx] : 0.0;
                double c = (idx < fr.corrcoef.size()) ? fr.corrcoef[idx] : 0.0;
                f << (fi + 1) << ',' << gx << ',' << gy << ',' << u << ',' << v << ',' << c << '\n';
            }
        }
    }
    LOG_INFO << "[singledic] wrote " << path;
    return true;
}

namespace {
// Write a 2D double matrix (row-major in `data`, dims rows x cols) into an open
// MAT file as a named variable. matio expects column-major, so we transpose.
bool put_matrix(mat_t* mat, const std::string& name, const std::vector<double>& data, size_t rows,
                size_t cols) {
    if (rows * cols != data.size()) return false;
    std::vector<double> col_major(rows * cols);
    for (size_t r = 0; r < rows; ++r)
        for (size_t c = 0; c < cols; ++c) col_major[c * rows + r] = data[r * cols + c];
    size_t dims[2] = {rows, cols};
    matvar_t* var =
        Mat_VarCreate(name.c_str(), MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims, col_major.data(), 0);
    if (!var) return false;
    Mat_VarWrite(mat, var, MAT_COMPRESSION_NONE);
    Mat_VarFree(var);
    return true;
}

bool put_scalar(mat_t* mat, const std::string& name, double value) {
    size_t dims[2] = {1, 1};
    matvar_t* var = Mat_VarCreate(name.c_str(), MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims, &value, 0);
    if (!var) return false;
    Mat_VarWrite(mat, var, MAT_COMPRESSION_NONE);
    Mat_VarFree(var);
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
// MAT writer — compact, MATLAB-loadable dump of the displacement grids.
// One file per trial (ncorr<n>.mat) with:
//   spacing (scalar), seed (1x2), Nframe (scalar),
//   U_<k>, V_<k>, C_<k>  (k = 1..Nframe)  each a grid_height x grid_width matrix
// This deliberately avoids the heavyweight ncorr-typed MatWriter API (which
// expects a ncorr::DIC_analysis_output we do not carry through the session
// path) and keeps the dependency surface small.
// ---------------------------------------------------------------------------
bool SingleDicWorkflow::writeMat(const SingleDicResult& res) const {
    const std::string path = cfg_.ncorrStem() + ".mat";
    mat_t* mat = Mat_CreateVer(path.c_str(), nullptr, MAT_FT_MAT5);
    if (!mat) {
        LOG_ERROR << "[singledic] cannot create MAT: " << path;
        return false;
    }

    put_scalar(mat, "spacing", static_cast<double>(res.subset_spacing));
    put_scalar(mat, "Nframe", static_cast<double>(res.frames.size()));
    if (res.seed_pw.size() >= 2) {
        std::vector<double> seed = {static_cast<double>(res.seed_pw[0]),
                                    static_cast<double>(res.seed_pw[1])};
        put_matrix(mat, "seed", seed, 1, 2);
    }

    for (size_t fi = 0; fi < res.frames.size(); ++fi) {
        const FrameResult& fr = res.frames[fi];
        const size_t rows = static_cast<size_t>(std::max(0, fr.grid_height));
        const size_t cols = static_cast<size_t>(std::max(0, fr.grid_width));
        const std::string k = std::to_string(fi + 1);
        if (rows * cols == fr.u.size() && !fr.u.empty()) {
            put_matrix(mat, "U_" + k, fr.u, rows, cols);
            if (fr.v.size() == rows * cols) put_matrix(mat, "V_" + k, fr.v, rows, cols);
            if (fr.corrcoef.size() == rows * cols)
                put_matrix(mat, "C_" + k, fr.corrcoef, rows, cols);
        }
    }

    Mat_Close(mat);
    LOG_INFO << "[singledic] wrote " << path;
    return true;
}

// ---------------------------------------------------------------------------
// Top-level driver — mirrors process_single_trial_ncorr.m (single camera).
// ---------------------------------------------------------------------------
SingleDicResult SingleDicWorkflow::run() {
    SingleDicResult result;

    LOG_INFO << "-------------------------------------------";
    LOG_INFO << "singledic: single-camera 2D DIC analysis";
    LOG_INFO << "  subject=" << cfg_.subject << " bloc=" << cfg_.bloc << " trial=" << cfg_.trial
             << " ref=" << cfg_.reftrial << " dir=" << cfg_.tracking_dir;

    // Ensure output directories exist.
    std::error_code ec;
    fs::create_directories(cfg_.outputTrialDir(), ec);
    fs::create_directories(cfg_.outputBlocDir(), ec);

    // 1. Import frames.
    std::vector<cv::Mat> raw;
    if (!importFrames(raw)) {
        result.message = "frame import failed";
        return result;
    }

    // 2. Saturate.
    std::vector<cv::Mat> satur = saturate(raw);

    // 3. ROI (load-or-full) on the reference (frame 0).
    cv::Mat roi = loadOrCreateRoi(satur.front());

    // 4. Optional bandpass filtering (uses ROI for bounds).
    std::vector<cv::Mat> proc = maybeFilter(satur, roi);

    // 5. Seed (load-or-center).
    std::vector<int> seed = loadOrCreateSeed(roi);
    LOG_INFO << "[singledic] seed = (" << (seed.size() > 0 ? seed[0] : 0) << ", "
             << (seed.size() > 1 ? seed[1] : 0) << ")";

    // NOTE on matching-to-reference: Vik's ncorr_matching2ref_single.m maps the
    // reference *trial's* ROI/seed onto the current trial via a one-frame DIC.
    // When do_matching is enabled and the reference trial differs from the
    // current trial, that mapping would be inserted here. The single-tracking
    // semantics below are unchanged; see report for the documented gap.
    if (cfg_.do_matching && cfg_.reftrial != cfg_.trial) {
        LOG_INFO << "[singledic] (matching-to-reference requested; using trial ROI/seed — "
                    "cross-trial mapping is a documented gap)";
    }

    // 6. Single tracking pass.
    if (!track(proc, roi, seed, result)) {
        if (result.message.empty()) result.message = "tracking failed";
        return result;
    }

    // 7. Write outputs.
    if (cfg_.write_csv) writeCsv(result);
    if (cfg_.write_mat) writeMat(result);

    LOG_INFO << "--> singledic analysis completed";
    return result;
}

} // namespace singledic
