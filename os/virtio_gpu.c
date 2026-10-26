/* Chapter 9 wrapper: same operations as rCore-Tutorial-v3/ch9 GPU driver. */
#include "defs.h"
#include "io.h"
#include "virtio_gpu.h"
#include "virtio_gpu_raw.h"

unsigned char *gpu_fb;
uint64 gpu_fb_len;

void virtio_gpu_init(void)
{
	/* LAB9_BEGIN virtio_gpu_init */
	irq_state_t state = io_irq_save();
	gpu_raw_init();
	gpu_fb = gpu_raw_setup_framebuffer(&gpu_fb_len);
	gpu_raw_setup_cursor();
	io_irq_restore(state);
	/* LAB9_END virtio_gpu_init */
}

unsigned char *virtio_gpu_get_framebuffer(uint64 *length)
{
	/* LAB9_BEGIN virtio_gpu_get_framebuffer */
	if (length)
		*length = gpu_fb_len;
	return gpu_fb;
	/* LAB9_END virtio_gpu_get_framebuffer */
}

void virtio_gpu_flush(void)
{
	/* LAB9_BEGIN virtio_gpu_flush */
	irq_state_t state = io_irq_save();
	gpu_raw_flush();
	io_irq_restore(state);
	/* LAB9_END virtio_gpu_flush */
}

void virtio_gpu_handle_irq(void)
{
	gpu_raw_ack();
}

uint64 gpu_framebuffer_size(void)
{
	return gpu_fb_len;
}

uint32 gpu_width(void)
{
	return gpu_driver.width;
}

uint32 gpu_height(void)
{
	return gpu_driver.height;
}
