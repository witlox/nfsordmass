# Top-level Makefile

obj-m += xprtrdma_kfi.o svcrdma_kfi.o

xprtrdma_kfi-y := src/kfi_transport.o \
                  src/kfi_verbs_compat.o \
                  src/kfi_ops.o \
                  src/kfi_memory.o \
                  src/kfi_completion.o \
                  src/kfi_connection.o \
                  src/kfi_progress.o \
                  src/kfi_key_mapping.o

svcrdma_kfi-y := src/svc_kfi_transport.o \
                 src/svc_kfi_ops.o

# Include paths - use $(src) which kbuild sets to the source directory
ccflags-y += -I$(src)/include

# Kernel build directory and current working directory
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# kfabric headers detection
# When called from kbuild, $(src) is set; otherwise use $(PWD)
ifdef KERNELRELEASE
    KFABRIC_WRAPPER := $(src)/external/kfabric-headers
    KFABRIC_LOCAL := $(src)/external/kfabric/include
    KFABRIC_SYSTEM := /usr/src/kfabric/include

    ifneq ($(wildcard $(KFABRIC_WRAPPER)/rdma/kfi/fabric.h),)
        ccflags-y += -I$(KFABRIC_WRAPPER)
        ccflags-y += -I$(KFABRIC_LOCAL)
        $(info Using local kfabric: $(KFABRIC_WRAPPER) + $(KFABRIC_LOCAL))
    else ifneq ($(wildcard $(KFABRIC_SYSTEM)/rdma/kfi/fabric.h),)
        ccflags-y += -I$(KFABRIC_SYSTEM)
        $(info Using system kfabric headers: $(KFABRIC_SYSTEM))
    else
        $(error kfabric headers not found. Run: ./scripts/setup_kfabric.sh)
    endif
else
    # Top-level make invocation - just check headers exist
    KFABRIC_WRAPPER := $(PWD)/external/kfabric-headers
    KFABRIC_LOCAL := $(PWD)/external/kfabric/include
    KFABRIC_SYSTEM := /usr/src/kfabric/include

    ifneq ($(wildcard $(KFABRIC_WRAPPER)/rdma/kfi/fabric.h),)
        $(info Using local kfabric: $(KFABRIC_WRAPPER) + $(KFABRIC_LOCAL))
    else ifneq ($(wildcard $(KFABRIC_SYSTEM)/rdma/kfi/fabric.h),)
        $(info Using system kfabric headers: $(KFABRIC_SYSTEM))
    else
        $(error kfabric headers not found. Run: ./scripts/setup_kfabric.sh)
    endif
endif

ccflags-y += -DCONFIG_SUNRPC_XPRT_RDMA_KFI
ccflags-y += -Wall -Werror

# External symbol dependencies (kfabric module)
KFABRIC_SYMVERS := $(PWD)/external/kfabric.symvers

all: modules

# Pre-clean stale build artifacts before building to avoid kbuild cache issues
modules:
	@rm -f src/.*.cmd .*.cmd 2>/dev/null || true
	@echo "Checking source files..."
	@test -f src/kfi_transport.c || (echo "ERROR: src/kfi_transport.c not found!" && exit 1)
	@test -f src/kfi_verbs_compat.c || (echo "ERROR: src/kfi_verbs_compat.c not found!" && exit 1)
	@echo "Source files OK, starting kernel build..."
	$(MAKE) -C $(KDIR) M=$(PWD) modules KBUILD_EXTRA_SYMBOLS="$(KFABRIC_SYMVERS)"

# Debug target to check paths and files
debug:
	@echo "=== Build Environment Debug ==="
	@echo "PWD: $(PWD)"
	@echo "KDIR: $(KDIR)"
	@echo "KFABRIC_WRAPPER: $(KFABRIC_WRAPPER)"
	@echo "KFABRIC_LOCAL: $(KFABRIC_LOCAL)"
	@echo ""
	@echo "=== Source Files ==="
	@ls -la src/*.c 2>/dev/null || echo "No .c files in src/"
	@echo ""
	@echo "=== Include Files ==="
	@ls -la include/*.h 2>/dev/null || echo "No .h files in include/"
	@echo ""
	@echo "=== External/kfabric ==="
	@ls -la external/ 2>/dev/null || echo "No external/ directory"

tests:
	@if [ -f tests/Makefile ]; then $(MAKE) -C tests; fi

clean:
	-$(MAKE) -C $(KDIR) M=$(PWD) clean 2>/dev/null || true
	@if [ -f tests/Makefile ]; then $(MAKE) -C tests clean 2>/dev/null || true; fi
	rm -f src/*.o src/.*.cmd
	rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order
	rm -rf .tmp_versions

# Deep clean - remove all generated files
distclean: clean
	rm -f src/*.o.* src/*.mod src/*.mod.c
	find . -name "*.o" -delete
	find . -name ".*.cmd" -delete
	find . -name "*.ko" -delete
	find . -name "*.mod" -delete
	find . -name "*.mod.c" -delete
	find . -name "Module.symvers" -delete
	find . -name "modules.order" -delete
	rm -rf .tmp_versions

install: modules
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a
	@echo "Modules installed. Load with:"
	@echo "  modprobe xprtrdma_kfi"
	@echo "  modprobe svcrdma_kfi"

load:
	./scripts/load_modules.sh

unload:
	-rmmod xprtrdma_kfi
	-rmmod svcrdma_kfi

test: tests
	./scripts/run_tests.sh

.PHONY: all modules tests clean distclean install load unload test debug
