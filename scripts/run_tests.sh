#!/bin/bash
# Test runner for kfabric NFS implementation

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TEST_DIR="$PROJECT_ROOT/tests"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "==================================="
echo "kfabric NFS Test Suite"
echo "==================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}WARNING: Not running as root. Will use sudo for module operations.${NC}"
    SUDO="sudo"
else
    SUDO=""
fi

# Track test results
UNIT_PASSED=0
UNIT_FAILED=0
INTEG_PASSED=0
INTEG_FAILED=0

run_test_module() {
    local module_path=$1
    local module_name
    local results
    local failures

    module_name=$(basename "$module_path" .ko)

    echo -n "  $module_name: "

    # Clear relevant dmesg
    $SUDO dmesg -C 2>/dev/null || true

    # Load module (it will run tests in init and return error to unload)
    if $SUDO insmod "$module_path" 2>/dev/null; then
        # Module loaded, remove it
        $SUDO rmmod "$module_name" 2>/dev/null || true
    fi

    # Check results from dmesg
    results=$($SUDO dmesg 2>/dev/null | grep -E "=== .* tests:" | tail -1)
    failures=$(echo "$results" | grep -oP '\d+(?= failures)' || echo "")

    if [ -z "$failures" ]; then
        echo -e "${YELLOW}UNKNOWN${NC} (check dmesg)"
        return 2
    elif [ "$failures" -eq 0 ]; then
        echo -e "${GREEN}PASSED${NC}"
        return 0
    else
        echo -e "${RED}FAILED${NC} ($failures failures)"
        return 1
    fi
}

# Check prerequisites (optional)
check_prerequisites() {
    echo "Checking prerequisites..."

    if ! lsmod | grep -q cxi 2>/dev/null; then
        echo -e "  ${YELLOW}CXI module not loaded (optional for unit tests)${NC}"
    else
        echo -e "  ${GREEN}CXI module loaded${NC}"
    fi

    if ! lsmod | grep -q kfabric 2>/dev/null; then
        echo -e "  ${YELLOW}kfabric module not loaded (optional for unit tests)${NC}"
    else
        echo -e "  ${GREEN}kfabric module loaded${NC}"
    fi

    echo ""
}

# Build tests
build_tests() {
    echo "Building tests..."
    cd "$TEST_DIR"

    if ! make modules 2>&1; then
        echo -e "${RED}Build failed!${NC}"
        exit 1
    fi

    echo -e "${GREEN}Tests built successfully${NC}"
    echo ""
}

# Run unit tests
run_unit_tests() {
    echo "==================================="
    echo "UNIT TESTS"
    echo "==================================="

    cd "$TEST_DIR"

    for test_ko in test_key_mapping.ko test_translate.ko test_memory.ko test_connection.ko test_errno.ko; do
        if [ -f "$test_ko" ]; then
            if run_test_module "$test_ko"; then
                ((UNIT_PASSED++))
            else
                ((UNIT_FAILED++))
            fi
        else
            echo -e "  ${YELLOW}$test_ko not found (skipped)${NC}"
        fi
    done

    echo ""
}

# Run integration tests
run_integration_tests() {
    echo "==================================="
    echo "INTEGRATION TESTS"
    echo "==================================="

    echo "Prerequisites for integration tests:"
    echo "  - NFS server running"
    echo "  - Mount point: /mnt/nfs_kfi_test"
    echo "  - xprtrdma_kfi module loaded"
    echo ""

    if [ ! -d "/mnt/nfs_kfi_test" ]; then
        echo -e "${YELLOW}Mount point not found. Skipping integration tests.${NC}"
        echo ""
        return
    fi

    cd "$TEST_DIR"

    if [ -f "test_loopback.ko" ]; then
        if run_test_module "test_loopback.ko"; then
            ((INTEG_PASSED++))
        else
            ((INTEG_FAILED++))
        fi
    else
        echo -e "  ${YELLOW}test_loopback.ko not found (skipped)${NC}"
    fi

    echo ""
}

# Print summary
print_summary() {
    echo "==================================="
    echo "TEST SUMMARY"
    echo "==================================="

    local total_passed=$((UNIT_PASSED + INTEG_PASSED))
    local total_failed=$((UNIT_FAILED + INTEG_FAILED))

    echo "Unit tests:        $UNIT_PASSED passed, $UNIT_FAILED failed"
    echo "Integration tests: $INTEG_PASSED passed, $INTEG_FAILED failed"
    echo "-----------------------------------"
    echo -n "Total:             $total_passed passed, $total_failed failed - "

    if [ "$total_failed" -eq 0 ] && [ "$total_passed" -gt 0 ]; then
        echo -e "${GREEN}SUCCESS${NC}"
        return 0
    elif [ "$total_failed" -gt 0 ]; then
        echo -e "${RED}FAILURE${NC}"
        return 1
    else
        echo -e "${YELLOW}NO TESTS RUN${NC}"
        return 2
    fi
}

# Main
main() {
    check_prerequisites
    build_tests
    run_unit_tests
    run_integration_tests
    print_summary

    echo ""
    echo "For detailed output: $SUDO dmesg | tail -200"
}

main "$@"
