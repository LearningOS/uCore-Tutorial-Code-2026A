#include "console.h"
#include "defs.h"
#include "loader.h"
#include "plic.h"
#include "timer.h"
#include "trap.h"
#include "virtio.h"
#include "io.h"
#include "virtio_gpu.h"
#include "virtio_input.h"

int dev_non_blocking_access;

void clean_bss()
{
	extern char s_bss[];
	extern char e_bss[];
	memset(s_bss, 0, e_bss - s_bss);
}

void main(uint64 hart_id, uint64 device_tree)
{
	clean_bss();
	if (hart_id != 0)
		panic("chapter 9 requires a single boot hart");
	timer_configure(device_tree);
	printf("hello world!\n");
	proc_init();
	kinit();
	kvm_init();
	trap_init();
	console_init();
	plicinit();
	virtio_disk_init();
	binit();
	fsinit();
	virtio_gpu_init();
	virtio_input_init();
	timer_init();
	load_init_app();
	infof("start scheduler!");
	show_all_files();
	dev_non_blocking_access = 1;
	printf("uCore chapter 9: interrupt-driven I/O ready\n");
	scheduler();
}
