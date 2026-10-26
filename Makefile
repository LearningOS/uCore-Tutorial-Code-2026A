.DEFAULT_GOAL := build
K := os
U := chapter9-user
F := nfs
TOOLPREFIX ?= riscv64-unknown-elf-
CC := $(TOOLPREFIX)gcc
LD := $(TOOLPREFIX)ld
OBJDUMP := $(TOOLPREFIX)objdump
PY ?= python3
QEMU ?= qemu-system-riscv64
GDB ?= gdb-multiarch
GDB_PORT ?= 15234
INIT_PROC ?= usershell
GUI ?= off
LOG ?= error
MODE ?= release
CFLAGS := -Wall -Werror -O1 -g -fno-omit-frame-pointer -ffreestanding -fno-builtin -fno-common -fno-stack-protector -fno-pie -nostdlib -mcmodel=medany -mno-relax -msmall-data-limit=0 -Ios -DLOG_LEVEL_$(shell echo $(LOG) | tr a-z A-Z)
ifeq ($(MODE),debug)
CFLAGS := $(filter-out -O1,$(CFLAGS)) -O0
endif
C_SRCS := $(wildcard os/*.c)
AS_SRCS := $(filter-out os/initproc.S,$(wildcard os/*.S)) os/initproc.S
OBJS := $(patsubst %.c,build/%.o,$(C_SRCS)) $(patsubst %.S,build/%.o,$(AS_SRCS))
.PHONY: build user fs-image run clean gdbserver gdbclient test-ch9 FORCE
build: build/kernel fs-image
user:
	$(MAKE) -C $(U) TOOLPREFIX=$(TOOLPREFIX)
fs-image: user nfs/fs
	./nfs/fs nfs/fs.img $(sort $(wildcard $(U)/target/bin/*))
nfs/fs: nfs/fs.c nfs/fs.h nfs/types.h
	cc -Wall -Werror -o $@ nfs/fs.c
os/initproc.S: FORCE
	$(PY) scripts/initproc.py $(INIT_PROC)
build/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
build/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
build/kernel: $(OBJS) os/kernel.ld
	$(LD) -z max-page-size=4096 -T os/kernel.ld -o $@ $(OBJS)
	$(OBJDUMP) -t $@ > build/kernel.sym
	@echo 'Build kernel done'
ifeq ($(GUI),on)
DISPLAY_OPTS :=
else
DISPLAY_OPTS := -display none
endif
QEMUOPTS = -machine virt -m 128M -smp 1 -bios bootloader/rustsbi-qemu.bin -kernel build/kernel -serial stdio -monitor none $(DISPLAY_OPTS) -global virtio-mmio.force-legacy=false -drive file=nfs/fs-copy.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.7 -device virtio-gpu-device,bus=virtio-mmio-bus.6,xres=1280,yres=800 -device virtio-keyboard-device,bus=virtio-mmio-bus.4 -device virtio-mouse-device,bus=virtio-mmio-bus.5
run: build
	cp nfs/fs.img nfs/fs-copy.img
	$(QEMU) $(QEMUOPTS)
gdbserver: build
	cp nfs/fs.img nfs/fs-copy.img
	$(QEMU) $(QEMUOPTS) -S -gdb tcp::$(GDB_PORT)
gdbclient:
	$(GDB) build/kernel -ex 'set architecture riscv:rv64' -ex 'target remote localhost:$(GDB_PORT)'
test-ch9: build
	$(PY) tools/run_ch9.py --qemu $(QEMU)
clean:
	rm -rf build chapter9-user/target nfs/fs nfs/*.img os/initproc.S
-include $(OBJS:.o=.d)
