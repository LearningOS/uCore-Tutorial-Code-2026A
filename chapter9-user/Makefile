.DEFAULT_GOAL := all
TOOLPREFIX ?= riscv64-unknown-elf-
CC := $(TOOLPREFIX)gcc
OBJCOPY := $(TOOLPREFIX)objcopy
CFLAGS := -Wall -Wextra -Werror -O2 -g -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -nostdlib -mcmodel=medany -mno-relax -msmall-data-limit=0 -Ilib
LDFLAGS := -nostdlib -static -T linker.ld -Wl,--build-id=none -Wl,--no-relax
PROGRAMS := usershell filetest_simple huge_write gui_shape
BINS := $(addprefix target/bin/,$(PROGRAMS))
COMMON := target/entry.o target/runtime.o
.PHONY: all build clean
all build: $(BINS)
target/entry.o: lib/entry.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
target/runtime.o: lib/runtime.c lib/user.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
target/%.o: src/%.c lib/user.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
target/%.elf: target/%.o $(COMMON) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(COMMON) $< -o $@
target/bin/%: target/%.elf
	@mkdir -p $(@D)
	$(OBJCOPY) -O binary $< $@
clean:
	rm -rf target
.SECONDARY:
