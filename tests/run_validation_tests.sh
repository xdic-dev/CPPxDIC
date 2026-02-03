#!/bin/bash

# DIC Pipeline Validation Test Runner
# This script runs all validation tests comparing C++ implementation against MATLAB reference data

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test configuration
BUILD_DIR="../build"
TEST_DATA_DIR="test_data"
REPORT_DIR="test_reports"

# Create directories if they don't exist
mkdir -p "$TEST_DATA_DIR"
mkdir -p "$REPORT_DIR"

echo "================================================"
echo "        DIC PIPELINE VALIDATION TESTS"
echo "================================================"
echo ""

# Function to run a test
run_test() {
    local test_name=$1
    local test_exec=$2
    local ref_file=$3
    local report_file=$4
    
    echo -n "Running $test_name... "
    
    if [ ! -f "$BUILD_DIR/bin/$test_exec" ]; then
        echo -e "${YELLOW}SKIPPED${NC} (executable not found - build first)"
        return 1
    fi
    
    if [ ! -f "$TEST_DATA_DIR/$ref_file" ]; then
        echo -e "${YELLOW}SKIPPED${NC} (reference data not found)"
        echo "  Please provide: $TEST_DATA_DIR/$ref_file"
        return 1
    fi
    
    # Run the test and capture output
    if "$BUILD_DIR/bin/$test_exec" "$TEST_DATA_DIR/$ref_file" > "$REPORT_DIR/$report_file" 2>&1; then
        echo -e "${GREEN}PASSED${NC}"
        return 0
    else
        echo -e "${RED}FAILED${NC}"
        echo "  See report: $REPORT_DIR/$report_file"
        return 1
    fi
}

# Build tests if needed
if [ ! -d "$BUILD_DIR" ] || [ "$1" == "--rebuild" ]; then
    echo "Building tests..."
    cd tests
    mkdir -p build
    cd build
    cmake ..
    make -j4
    cd ../..
    echo "Build complete."
    echo ""
fi

# Track overall results
TOTAL_TESTS=0
PASSED_TESTS=0

# Unit Tests
echo "=== UNIT TESTS ==="
echo ""

# Test 1: TCPE Deformation Algorithm
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if run_test "TCPE Deformation" "test_deformation_validation" \
    "deformation_reference.mat" "deformation_validation.txt"; then
    PASSED_TESTS=$((PASSED_TESTS + 1))
fi

# Test 2: Ben's Image Filter
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if run_test "Ben's Image Filter" "test_filter_validation" \
    "filter_reference.mat" "filter_validation.txt"; then
    PASSED_TESTS=$((PASSED_TESTS + 1))
fi

# Test 3: Temporal Filtering
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if run_test "Temporal Filtering" "test_temporal_validation" \
    "temporal_reference.mat" "temporal_validation.txt"; then
    PASSED_TESTS=$((PASSED_TESTS + 1))
fi

echo ""
echo "=== INTEGRATION TESTS ==="
echo ""

# Test 4: Complete Pipeline
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if run_test "Complete Pipeline" "test_pipeline_validation" \
    "." "pipeline_validation.txt"; then
    PASSED_TESTS=$((PASSED_TESTS + 1))
fi

# Test 5: 3D Reconstruction
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if [ -f "$TEST_DATA_DIR/DIC3Dcombined_cpp.mat" ] && [ -f "$TEST_DATA_DIR/DIC3Dcombined_matlab.mat" ]; then
    echo -n "Running 3D Reconstruction comparison... "
    if "$BUILD_DIR/bin/test_mat_comparator" \
        "$TEST_DATA_DIR/DIC3Dcombined_cpp.mat" \
        "$TEST_DATA_DIR/DIC3Dcombined_matlab.mat" > "$REPORT_DIR/3d_comparison.txt" 2>&1; then
        echo -e "${GREEN}PASSED${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}FAILED${NC}"
    fi
else
    echo "Running 3D Reconstruction comparison... ${YELLOW}SKIPPED${NC} (data not found)"
fi

# Test 6: Deformation Results
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if [ -f "$TEST_DATA_DIR/DIC3DPPresults_cpp.mat" ] && [ -f "$TEST_DATA_DIR/DIC3DPPresults_matlab.mat" ]; then
    echo -n "Running Deformation Results comparison... "
    if "$BUILD_DIR/bin/test_mat_comparator" \
        "$TEST_DATA_DIR/DIC3DPPresults_cpp.mat" \
        "$TEST_DATA_DIR/DIC3DPPresults_matlab.mat" > "$REPORT_DIR/deform_comparison.txt" 2>&1; then
        echo -e "${GREEN}PASSED${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}FAILED${NC}"
    fi
else
    echo "Running Deformation Results comparison... ${YELLOW}SKIPPED${NC} (data not found)"
fi

echo ""
echo "================================================"
echo "                SUMMARY"
echo "================================================"
echo ""

# Calculate percentage
if [ $TOTAL_TESTS -gt 0 ]; then
    PERCENTAGE=$((100 * PASSED_TESTS / TOTAL_TESTS))
else
    PERCENTAGE=0
fi

# Display summary with color coding
if [ $PASSED_TESTS -eq $TOTAL_TESTS ]; then
    echo -e "${GREEN}ALL TESTS PASSED!${NC}"
elif [ $PASSED_TESTS -eq 0 ]; then
    echo -e "${RED}ALL TESTS FAILED!${NC}"
else
    echo -e "${YELLOW}PARTIAL SUCCESS${NC}"
fi

echo "Tests Passed: $PASSED_TESTS/$TOTAL_TESTS ($PERCENTAGE%)"
echo ""

# Generate consolidated report
echo "Generating consolidated report..."
cat > "$REPORT_DIR/summary.txt" <<EOF
DIC PIPELINE VALIDATION SUMMARY
================================
Date: $(date)
Tests Passed: $PASSED_TESTS/$TOTAL_TESTS ($PERCENTAGE%)

Individual Test Reports:
------------------------
EOF

for report in "$REPORT_DIR"/*.txt; do
    if [ "$report" != "$REPORT_DIR/summary.txt" ]; then
        echo "- $(basename $report)" >> "$REPORT_DIR/summary.txt"
    fi
done

echo "Reports saved in: $REPORT_DIR/"
echo ""

# Exit with appropriate code
if [ $PASSED_TESTS -eq $TOTAL_TESTS ]; then
    exit 0
else
    exit 1
fi
