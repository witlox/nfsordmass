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

# Include paths
ccflags-y += -I$(src)/include

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# kfabric headers - use PWD for detection, src for actual includes
# During kernel build, $(src) is set by kbuild, use $(M) or absolute path for detection
ifdef KERNELRELEASE
    # We're being called from kernel build system, $(src) is valid
    KFABRIC_WRAPPER := $(src)/external/kfabric-headers
    KFABRIC_LOCAL := $(src)/external/kfabric/include
else
    # Top-level make, use PWD
    KFABRIC_WRAPPER := $(PWD)/external/kfabric-headers
    KFABRIC_LOCAL := $(PWD)/external/kfabric/include
endif

KFABRIC_SYSTEM := /usr/src/kfabric/include

# Check for local kfabric (wrapper + actual headers)
ifneq ($(wildcard $(KFABRIC_WRAPPER)/rdma/kfi/fabric.h),)
    ccflags-y += -I$(KFABRIC_WRAPPER)
    ccflags-y += -I$(KFABRIC_LOCAL)
    $(info Using local kfabric: $(KFABRIC_WRAPPER) + $(KFABRIC_LOCAL))
# Check for system kfabric
else ifneq ($(wildcard $(KFABRIC_SYSTEM)/rdma/kfi/fabric.h),)
    ccflags-y += -I$(KFABRIC_SYSTEM)
    $(info Using system kfabric headers: $(KFABRIC_SYSTEM))
else
    $(error kfabric headers not found. Run: ./scripts/setup_kfabric.sh)
endif

ccflags-y += -DCONFIG_SUNRPC_XPRT_RDMA_KFI
ccflags-y += -Wall -Werror

# External symbol dependencies (kfabric module)
KFABRIC_SYMVERS := $(PWD)/external/kfabric.symvers

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules KBUILD_EXTRA_SYMBOLS="$(KFABRIC_SYMVERS)"

tests:
	@if [ -f tests/Makefile ]; then $(MAKE) -C tests; fi

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	@if [ -f tests/Makefile ]; then $(MAKE) -C tests clean; fi

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

.PHONY: all modules tests clean install load unload test
