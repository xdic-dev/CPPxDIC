/**
 * @file test_xdic_mode.cpp
 * @brief Unit tests for the compile-time xDIC mode guard (include/xdic/xdic_mode.h).
 *
 * In the default build (XDIC_MODE=camerapairs) the active mode must resolve to
 * CameraPairs / "camerapairs". This is a compile-time guarantee; we assert it both at
 * compile time (static_assert) and at runtime.
 */

#include "../framework/test_harness.h"
#include "xdic/xdic_mode.h"

#include <cstring>

// Compile-time guard: in the default (and CI) build this header falls back to camerapairs.
#if !defined(XDIC_MODE_MIRRORED) && !defined(XDIC_MODE_MULTI)
static_assert(xdic::active_mode() == xdic::Mode::CameraPairs,
              "default build must select camerapairs mode");
#endif

TEST(xdic_mode, default_is_camerapairs) {
#if defined(XDIC_MODE_MIRRORED)
    CHECK_TRUE(xdic::active_mode() == xdic::Mode::Mirrored);
    CHECK_TRUE(std::strcmp(xdic::active_mode_name(), "mirrored") == 0);
#elif defined(XDIC_MODE_MULTI)
    CHECK_TRUE(xdic::active_mode() == xdic::Mode::Multi);
    CHECK_TRUE(std::strcmp(xdic::active_mode_name(), "multi") == 0);
#else
    CHECK_TRUE(xdic::active_mode() == xdic::Mode::CameraPairs);
    CHECK_TRUE(std::strcmp(xdic::active_mode_name(), "camerapairs") == 0);
#endif
}

TEST(xdic_mode, name_is_nonempty) {
    CHECK_TRUE(xdic::active_mode_name() != nullptr);
    CHECK_TRUE(std::strlen(xdic::active_mode_name()) > 0);
}

TEST_MAIN()
