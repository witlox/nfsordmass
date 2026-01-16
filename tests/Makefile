# Tests Makefile
# Kernel module unit and integration tests for kfabric NFS transport

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Unit test modules
obj-m += test_key_mapping.o
obj-m += test_translate.o
obj-m += test_memory.o
obj-m += test_connection.o
obj-m += test_errno.o

# Integration test modules
obj-m += test_loopback.o

# Source paths
test_key_mapping-y := unit/test_key_mapping.o
test_translate-y := unit/test_translate.o
test_memory-y := unit/test_memory.o
test_connection-y := unit/test_connection.o
test_errno-y := unit/test_errno.o
test_loopback-y := integration/test_loopback.o

# Include paths - parent project headers
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../src

# kfabric headers - check during kernel build
ifdef KERNELRELEASE
    KFABRIC_WRAPPER := $(src)/../external/kfabric-headers
    KFABRIC_LOCAL := $(src)/../external/kfabric/include
else
    KFABRIC_WRAPPER := $(PWD)/../external/kfabric-headers
    KFABRIC_LOCAL := $(PWD)/../external/kfabric/include
endif

ifneq ($(wildcard $(KFABRIC_WRAPPER)/rdma/kfi/fabric.h),)
    ccflags-y += -I$(KFABRIC_WRAPPER)
    ccflags-y += -I$(KFABRIC_LOCAL)
endif

# Build flags
ccflags-y += -DCONFIG_SUNRPC_XPRT_RDMA_KFI

# Default target - just print info
all:
	@echo "=== kfabric NFS Transport Test Suite ==="
	@echo ""
	@echo "Available targets:"
	@echo "  make modules    - Build all test modules"
	@echo "  make clean      - Clean build artifacts"
	@echo ""
	@echo "To run tests after building:"
	@echo "  insmod test_key_mapping.ko    # Key mapping tests"
	@echo "  insmod test_translate.ko      # Translation tests"
	@echo "  insmod test_memory.ko         # Memory tests"
	@echo "  insmod test_connection.ko     # Connection tests"
	@echo "  insmod test_loopback.ko       # Integration tests (requires NFS setup)"
	@echo ""
	@echo "Test results appear in dmesg/kernel log"

# Build test modules
modules:
	@echo "Building test modules..."
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Clean build artifacts
clean:
	@echo "Cleaning test build artifacts..."
	-$(MAKE) -C $(KDIR) M=$(PWD) clean 2>/dev/null || true
	rm -f *.o *.ko *.mod.c *.mod *.symvers *.order .*.cmd
	rm -f unit/*.o unit/.*.cmd
	rm -f integration/*.o integration/.*.cmd
	rm -rf .tmp_versions

# Run unit tests (requires root)
run-unit: modules
	@echo "Running unit tests..."
	-insmod test_key_mapping.ko 2>/dev/null; rmmod test_key_mapping 2>/dev/null || true
	-insmod test_translate.ko 2>/dev/null; rmmod test_translate 2>/dev/null || true
	-insmod test_memory.ko 2>/dev/null; rmmod test_memory 2>/dev/null || true
	-insmod test_connection.ko 2>/dev/null; rmmod test_connection 2>/dev/null || true
	-insmod test_errno.ko 2>/dev/null; rmmod test_errno 2>/dev/null || true
	@echo "Check dmesg for test results"

# Run integration tests (requires NFS setup)
run-integration: modules
	@echo "Running integration tests..."
	@echo "NOTE: Requires /mnt/nfs_kfi_test to be mounted"
	-insmod test_loopback.ko 2>/dev/null; rmmod test_loopback 2>/dev/null || true
	@echo "Check dmesg for test results"

.PHONY: all modules clean run-unit run-integration
