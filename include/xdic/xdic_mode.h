/**
 * @file xdic_mode.h
 * @brief Compile-time selection of the xDIC reconstruction mode.
 *
 * The xDIC executable supports (or will support) three reconstruction modes:
 *   - camerapairs : stereo pairs of cameras (the only fully-working mode today).
 *   - mirrored    : a single camera plus a mirror rig (MNG-prefixed MultiDIC files). STUB.
 *   - multi       : N-camera generalisation of the camera-pair approach. STUB.
 *
 * The active mode is chosen at configure time via the CMake cache variable `XDIC_MODE`
 * (default `camerapairs`), which defines exactly one of the following preprocessor macros:
 *   - XDIC_MODE_CAMERAPAIRS
 *   - XDIC_MODE_MIRRORED
 *   - XDIC_MODE_MULTI
 *
 * This header centralises that selection so future modes slot in without restructuring the
 * working camerapairs pipeline. If no macro is defined (e.g. building a translation unit
 * outside CMake), it falls back to camerapairs to preserve current behaviour.
 */

#pragma once

// Fall back to the working mode when none was defined by the build system.
#if !defined(XDIC_MODE_CAMERAPAIRS) && !defined(XDIC_MODE_MIRRORED) && !defined(XDIC_MODE_MULTI)
#define XDIC_MODE_CAMERAPAIRS
#endif

namespace xdic {

/// Enumeration of the supported xDIC reconstruction modes.
enum class Mode {
    CameraPairs, ///< Stereo camera pairs (default, fully implemented).
    Mirrored,    ///< Single camera + mirror rig (stub).
    Multi        ///< N-camera generalisation (stub).
};

/**
 * @brief The reconstruction mode selected at compile time.
 * @return The active xdic::Mode for this build.
 */
constexpr Mode active_mode() {
#if defined(XDIC_MODE_MIRRORED)
    return Mode::Mirrored;
#elif defined(XDIC_MODE_MULTI)
    return Mode::Multi;
#else
    return Mode::CameraPairs;
#endif
}

/**
 * @brief Human-readable name of the active mode.
 * @return A static string: "camerapairs", "mirrored", or "multi".
 */
constexpr const char* active_mode_name() {
#if defined(XDIC_MODE_MIRRORED)
    return "mirrored";
#elif defined(XDIC_MODE_MULTI)
    return "multi";
#else
    return "camerapairs";
#endif
}

} // namespace xdic
