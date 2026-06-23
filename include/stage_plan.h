/**
 * StagePlan — selects which pipeline sub-steps a single process executes.
 *
 * The full pipeline is preprocessing -> Step D (matching + per-camera tracking +
 * format) -> Step E (3D reconstruction) -> Step F (deformation). Because every
 * sub-step already checkpoints its outputs to disk (and downstream sub-steps
 * load those checkpoints), the pipeline can be decomposed into independently
 * schedulable processes — e.g. one SLURM array task per stage. A StagePlan tells
 * a run which sub-steps to perform; everything else is satisfied from existing
 * checkpoints on disk.
 *
 * All fields default to "do everything", so a run with no `--stages` selection
 * behaves exactly as before (full pipeline).
 */
#ifndef STAGE_PLAN_H
#define STAGE_PLAN_H

#include <sstream>
#include <string>

struct StagePlan {
    // Step D sub-steps
    bool match = true;  ///< ROI/seed + REF->trial matching + camera-to-camera matching
    bool track = true;  ///< per-camera NCorr tracking
    bool format = true; ///< combine ncorr outputs -> myDIC2DpairResults
    // Step E / F
    bool recon = true;  ///< Step E: 3D reconstruction
    bool deform = true; ///< Step F: deformation / strain

    // Optional narrowing for embarrassingly-parallel array tasks.
    int only_pair = 0; ///< 0 = all stereopairs; otherwise restrict to this pair
    int only_cam = 0;  ///< 0 = both cameras; otherwise restrict tracking to this camera id

    /// True if any Step-D sub-step is requested.
    bool anyStepD() const { return match || track || format; }

    /**
     * Parse a comma-separated stage spec into @p out (stage booleans only;
     * only_pair/only_cam are left untouched so the caller can set them from
     * --pair/--cam). When a spec is given, selection is explicit: every stage
     * starts false and is enabled by the tokens below.
     *
     * Tokens (case-insensitive):
     *   all                      -> match,track,format,recon,deform
     *   d                        -> match,track,format   (full Step D)
     *   match | matching         -> match
     *   track | tracking         -> track
     *   format                   -> format
     *   e | recon                -> recon
     *   f | deform               -> deform
     *
     * @return false (and sets *err) on an unrecognised token.
     */
    static bool parse(const std::string& spec, StagePlan& out, std::string* err) {
        out.match = out.track = out.format = out.recon = out.deform = false;
        std::stringstream ss(spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            // trim + lowercase
            size_t b = tok.find_first_not_of(" \t");
            size_t e = tok.find_last_not_of(" \t");
            if (b == std::string::npos) continue;
            tok = tok.substr(b, e - b + 1);
            for (char& c : tok) c = static_cast<char>(::tolower(c));

            if (tok == "all") {
                out.match = out.track = out.format = out.recon = out.deform = true;
            } else if (tok == "d") {
                out.match = out.track = out.format = true;
            } else if (tok == "match" || tok == "matching") {
                out.match = true;
            } else if (tok == "track" || tok == "tracking") {
                out.track = true;
            } else if (tok == "format") {
                out.format = true;
            } else if (tok == "e" || tok == "recon") {
                out.recon = true;
            } else if (tok == "f" || tok == "deform") {
                out.deform = true;
            } else {
                if (err) *err = "unknown stage token: '" + tok + "'";
                return false;
            }
        }
        return true;
    }
};

#endif // STAGE_PLAN_H
