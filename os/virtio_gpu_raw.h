#ifndef VIRTIO_GPU_RAW_H
#define VIRTIO_GPU_RAW_H

#include "types.h"
#include "virtio_mmio.h"

/* Provided VirtIO library layer, corresponding to virtio-drivers@4ee80e5. */
struct gpu_raw_device {
	struct vmmio_device device;
	struct virtqueue control_queue;
	struct virtqueue cursor_queue;
	unsigned char *framebuffer;
	uint64 length;
	uint32 width, height;
};

extern struct gpu_raw_device gpu_driver;
void gpu_raw_init(void);
unsigned char *gpu_raw_setup_framebuffer(uint64 *length);
void gpu_raw_setup_cursor(void);
void gpu_raw_flush(void);
void gpu_raw_ack(void);
#endif
