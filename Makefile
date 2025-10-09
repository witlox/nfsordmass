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

ccflags-y += -I$(src)/include
ccflags-y += -I/usr/src/kfabric/include
ccflags-y += -DCONFIG_SUNRPC_XPRT_RDMA_KFI
ccflags-y += -Wall -Werror

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: modules tests

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

tests:
	$(MAKE) -C tests

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(MAKE) -C tests clean

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
