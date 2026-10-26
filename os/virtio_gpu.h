#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H
#include "types.h"

/* The same device-owned framebuffer is exposed throughout the GPU lifetime. */
extern unsigned char *gpu_fb;
extern uint64 gpu_fb_len;
void virtio_gpu_init(void);
unsigned char *virtio_gpu_get_framebuffer(uint64 *length);
void virtio_gpu_flush(void);
void virtio_gpu_handle_irq(void);
uint64 gpu_framebuffer_size(void);
uint32 gpu_width(void);
uint32 gpu_height(void);
#endif
