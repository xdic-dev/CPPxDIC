/**
 * @file singledic_config.h
 * @brief Single-camera (2D, no-stereo) configuration adapter for CPPxDIC.
 *
 * `singledic` reproduces Viktoriia's single-camera "sliding analysis" step-D
 * workflow (see apps/singledic/README_singledic.md for the precise MATLAB
 * references). That workflow differs from the standard stereo `xdic`
 * camerapairs pipeline only in the 2D-DIC stage (step D): there is exactly ONE
 * camera, ONE reference frame, a single ROI/seed, an optional matching-to-
 * reference-trial step, and a single tracking pass — there is no stereo pairing
 * and no stepE/stepF 3D reconstruction.
 *
 * Vik's on-disk layout is `<base>/data/vid/<subject>/<bloc>/<trial>.mp4` for
 * inputs and `<base>/analysis/<subject>/<bloc>/<trial>/` for results, keyed by
 * (subject, bloc, trial) instead of the stereo pipeline's
 * (subject, material, trial, phase, pair). Rather than editing the shared
 * `Config` (top-level include/config.h) to add this layout, this adapter WRAPS
 * a `Config` and derives a single-camera sub-path scheme on top of it. The
 * underlying `Config` still owns all DIC/ncorr/visualisation parameters, so
 * `singledic` reuses the exact same tunables as the production engine.
 *
 * @note No shared production file is modified by this header. If the team wants
 *       a first-class single-camera mode, the recommended (NOT-made) change is
 *       to add an optional `single_camera` sub-path block to `Config` mirroring
 *       the fields below; see the task report.
 */

#ifndef SINGLEDIC_CONFIG_H
#define SINGLEDIC_CONFIG_H

#include "config.h"
#include "parameters.h"

#include <string>

namespace singledic {

/**
 * @brief Wraps a CPPxDIC `Config` and adds a single-camera sub-path scheme.
 *
 * Construction copies the supplied `Config` (so all engine tunables — step_d
 * radius/spacing/cutoffs, ncorr params, units_per_pixel, etc. — are inherited)
 * and overlays single-camera-specific naming. Inputs and outputs are addressed
 * by (subject, bloc, trial); a single reference trial is shared across trials
 * of the same (subject, bloc), exactly like Vik's process_single_trial_ncorr.m.
 */
class SinglediConfig {
public:
    /// Underlying engine configuration (owns all DIC/ncorr/viz parameters).
    Config base;

    // --- Single-camera identity (Vik: subject / bloc / trial) ---------------
    std::string subject = "S01";       ///< Subject folder name (e.g. "S01").
    std::string bloc = "bloc1";        ///< Bloc / block folder name.
    std::string trial = "001";         ///< Current trial name (video stem).
    std::string reftrial = "001";      ///< Reference trial name (shared per bloc).

    // --- Frame window & tracking (Vik: idx_frame_start/end, jump, dir) ------
    int idx_frame_start = 1;           ///< First frame to analyse (1-based, MATLAB-style).
    int idx_frame_end = 0;             ///< Last frame (0 = "to end of sequence").
    int frame_jump = 1;                ///< Frame stride.
    /// Tracking direction: "forward" or "backward" (backward flips the sequence).
    std::string tracking_dir = "forward";
    /// ncorr_number tag (1/2) used to name outputs, mirroring forward/backward passes.
    int ncorr_number = 1;

    // --- Saturation bounds (Vik: LIMIT_GRAYSCALE_LOW / HIGH) -----------------
    int limit_grayscale_low = 40;      ///< Lower saturation clamp (satur low).
    int limit_grayscale_high = 140;    ///< Upper saturation clamp (satur high).

    // --- Bandpass filter cutoffs (Vik: param_filt_im = [low, high]) ----------
    bool filter_im_mode = false;       ///< Apply bandpass+normalise filtering before DIC.
    int param_filt_low = 50;           ///< Bandpass low cutoff.
    int param_filt_high = 200;         ///< Bandpass high cutoff.

    // --- Step-D feature toggles ---------------------------------------------
    /// If true, run a matching-to-reference-trial pass before tracking (Vik
    /// ncorr_matching2ref_single.m); maps the ROI/seed of the reference trial
    /// onto the current trial. If false, the trial's own ROI/seed is used.
    bool do_matching = false;

    /// Whether to write a MATLAB-compatible .mat sidecar next to outputs.
    bool write_mat = true;
    /// Whether to write a CSV of the tracked feature trajectories.
    bool write_csv = true;

    SinglediConfig() = default;
    explicit SinglediConfig(const Config& cfg) : base(cfg) {}

    // ------------------------------------------------------------------------
    // Path helpers — single-camera layout derived from base.data_path /
    // base.dic_path. Vik input:  <data>/vid/<subject>/<bloc>/<trial>(.mp4|/)
    //          Vik output: <dic>/<subject>/<bloc>/<trial>/
    // We keep these as overridable sub-paths so callers may point at any layout.
    // ------------------------------------------------------------------------

    /// Root directory that holds per-subject video folders (default "<data>/vid").
    std::string vid_root() const {
        return base.data_path.empty() ? std::string("vid")
                                       : base.data_path + "/vid";
    }

    /// Directory for the current trial's input (subject/bloc/trial).
    std::string inputTrialDir() const {
        return vid_root() + "/" + subject + "/" + bloc + "/" + trial;
    }

    /// Directory for the reference trial's input (subject/bloc/reftrial).
    std::string inputRefTrialDir() const {
        return vid_root() + "/" + subject + "/" + bloc + "/" + reftrial;
    }

    /// Output directory for the current trial (subject/bloc/trial under dic_path).
    std::string outputTrialDir() const {
        const std::string root = base.dic_path.empty() ? std::string("analysis")
                                                        : base.dic_path;
        return root + "/" + subject + "/" + bloc + "/" + trial;
    }

    /// Output directory shared per (subject, bloc) — holds ROI/seed reference files.
    std::string outputBlocDir() const {
        const std::string root = base.dic_path.empty() ? std::string("analysis")
                                                        : base.dic_path;
        return root + "/" + subject + "/" + bloc;
    }

    /// ROI mask file, shared per bloc and keyed by reference trial.
    /// Mirrors Vik: REF_MASK_<reftrial>.mat
    std::string roiFile() const {
        return outputBlocDir() + "/REF_MASK_" + reftrial + ".mat";
    }

    /// Seed file, shared per bloc, keyed by reference trial and direction.
    /// Mirrors Vik: REF_SEED_<reftrial>_<dir>.mat
    std::string seedFile() const {
        return outputBlocDir() + "/REF_SEED_" + reftrial + "_" + tracking_dir + ".mat";
    }

    /// Matching-to-reference cache file. Mirrors Vik: MATCHING2<reftrial>.mat/.bin
    std::string matchingFile(const std::string& ext = ".bin") const {
        return outputTrialDir() + "/MATCHING2" + reftrial + ext;
    }

    /// ncorr tracking output stem (no extension). Mirrors Vik: ncorr<ncorr_number>
    std::string ncorrStem() const {
        return outputTrialDir() + "/ncorr" + std::to_string(ncorr_number);
    }

    /**
     * @brief Build a `BaseParameters` view so existing CPPxDIC helpers
     *        (ROIManager, ImageProcessor, MatWriter) can be reused unchanged.
     *
     * Maps the single-camera identity onto the stereo struct's fields:
     *   material <- bloc, stereopair <- 1, phase <- tracking_dir,
     *   cam_1 = cam_2 <- 1 (single camera).
     */
    cppxdic::BaseParameters toBaseParameters() const {
        cppxdic::BaseParameters bp;
        bp.baseDataPath = vid_root();
        bp.baseResultPath = base.dic_path;
        bp.outputPath = outputTrialDir();
        bp.subject = subject;
        bp.material = bloc;       // reuse 'material' slot for bloc
        bp.trial = trial;
        bp.stereopair = 1;        // single camera => single (degenerate) pair
        bp.phase = tracking_dir;  // reuse 'phase' slot for tracking direction
        bp.reftrial = reftrial;
        bp.roifile = roiFile();
        bp.matchingfile = matchingFile();
        bp.seedfile = seedFile();
        bp.jump = frame_jump;
        bp.idxstart_set = idx_frame_start;
        bp.idxend_set = idx_frame_end;
        bp.limit_grayscale = limit_grayscale_high;
        bp.cam_1 = 1;
        bp.cam_2 = 1;
        return bp;
    }
};

}  // namespace singledic

#endif  // SINGLEDIC_CONFIG_H
