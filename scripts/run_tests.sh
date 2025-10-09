#!/bin/bash
# Test runner for kfabric NFS implementation

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TEST_DIR="$PROJECT_ROOT/tests"

echo "==================================="
echo "kfabric NFS Test Suite"
echo "==================================="

# Check prerequisites
echo "Checking prerequisites..."

if ! lsmod | grep -q cxi; then
    echo "ERROR: CXI kernel module not loaded"
    exit 1
fi

if ! lsmod | grep -q kfabric; then
    echo "ERROR: kfabric not loaded"
    exit 1
fi

echo "Prerequisites OK"
echo ""

# Build tests
echo "Building tests..."
cd "$TEST_DIR"
make clean
make
echo "Tests built"
echo ""

# Run unit tests
echo "==================================="
echo "UNIT TESTS"
echo "==================================="

echo "Loading test_translation..."
sudo insmod unit/test_translation.ko
sleep 1
sudo rmmod test_translation
echo ""

echo "Loading test_key_mapping..."
sudo insmod unit/test_key_mapping.ko
sleep 1
sudo rmmod test_key_mapping
echo ""

# Check unit test results from dmesg
echo "Unit test results:"
sudo dmesg | tail -50 | grep -E "(PASS|FAIL):"
echo ""

# Run integration tests (requires setup)
echo "==================================="
echo "INTEGRATION TESTS"
echo "==================================="

echo "NOTE: Integration tests require:"
echo "  - NFS server running"
echo "  - VNI allocated"
echo "  - Export configured"
echo ""
read -p "Are prerequisites met? (y/n) " -n 1 -r
echo ""

if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Loading test_loopback..."
    sudo insmod integration/test_loopback.ko
    sleep 5
    sudo rmmod test_loopback
    
    echo "Integration test results:"
    sudo dmesg | tail -100 | grep -E "(TEST:|PASS:|FAIL:)"
else
    echo "Skipping integration tests"
fi

echo ""
echo "==================================="
echo "Test run complete"
echo "==================================="
echo "Full logs: sudo dmesg | tail -200"
