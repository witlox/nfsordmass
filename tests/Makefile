# Tests Makefile
# These are kernel module tests that can be built and loaded

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Test modules
obj-m += test_key_mapping.o
obj-m += test_translate.o
obj-m += test_loopback.o

# Source paths
test_key_mapping-y := unit/test_key_mapping.o
test_translate-y := unit/test_translate.o
test_loopback-y := integration/test_loopback.o

# Include paths - parent project headers
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../src

# kfabric headers (same as parent)
KFABRIC_WRAPPER := $(PWD)/../external/kfabric-headers
KFABRIC_LOCAL := $(PWD)/../external/kfabric/include

ifneq ($(wildcard $(KFABRIC_WRAPPER)/rdma/kfi/fabric.h),)
    ccflags-y += -I$(KFABRIC_WRAPPER)
    ccflags-y += -I$(KFABRIC_LOCAL)
endif

all:
	@echo "Test modules can be built with: make modules"
	@echo "Skipping test build for now (requires kernel build environment)"

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean 2>/dev/null || true
	rm -f *.o *.ko *.mod.c *.mod *.symvers *.order
	rm -f unit/*.o integration/*.o

.PHONY: all modules clean
