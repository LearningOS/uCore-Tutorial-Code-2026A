/* Supplied GPU protocol implementation. Operation sequence and pixel format
 * follow rcore-os/virtio-drivers@4ee80e5/src/gpu.rs (MIT licensed).
 * Students implement the three wrapper methods in virtio_gpu.c. */
#include "defs.h"
#include "virtio_gpu_raw.h"
#include "virtio_gpu_cursor.h"

#define GPU_BASE 0x10007000UL
#define GPU_RESOURCE_FB 0xbabe
#define GPU_RESOURCE_CURSOR 0xdade
#define GPU_FORMAT_B8G8R8A8_UNORM 1
#define GPU_OK_NODATA 0x1100
#define GPU_OK_DISPLAY_INFO 0x1101
#define GPU_FRAMEBUFFER_CAPACITY (1920UL * 1080 * 4)

struct gpu_header {
	uint32 type, flags;
	uint64 fence_id;
	uint32 context_id, padding;
};
struct gpu_rect { uint32 x, y, width, height; };
struct gpu_scanout { struct gpu_rect rect; uint32 enabled, flags; };
struct gpu_display_info {
	struct gpu_header header;
	struct gpu_scanout scanouts[16];
};

_Static_assert(sizeof(struct gpu_header) == 24, "VirtIO GPU header layout");
_Static_assert(sizeof(struct gpu_display_info) == 408, "VirtIO GPU display-info layout");

struct gpu_raw_device gpu_driver;
static unsigned char framebuffer[PGROUNDUP(GPU_FRAMEBUFFER_CAPACITY)]
	__attribute__((aligned(4096)));
static unsigned char cursor_buffer[64 * 64 * 4]
	__attribute__((aligned(4096)));

static struct gpu_header header(uint32 type)
{
	struct gpu_header result = {0};
	result.type = type;
	return result;
}

static void gpu_request(void *request, uint32 request_len, void *response,
			uint32 response_len, uint32 expected_type)
{
	memset(response, 0, response_len);
	struct vq_buf buffers[2] = {
		{request, request_len, 0}, {response, response_len, 1}
	};
	vq_request(&gpu_driver.control_queue, buffers, 2);
	if (((struct gpu_header *)response)->type != expected_type)
		panic("GPU response 0x%x expected 0x%x",
		      ((struct gpu_header *)response)->type, expected_type);
}

static void resource_create(uint32 id, uint32 width, uint32 height)
{
	struct {
		struct gpu_header header;
		uint32 resource_id, format, width, height;
	} request = {header(0x101), id, GPU_FORMAT_B8G8R8A8_UNORM, width, height};
	struct gpu_header response;
	gpu_request(&request, sizeof(request), &response, sizeof(response),
		    GPU_OK_NODATA);
}

static void resource_attach(uint32 id, void *memory, uint32 length)
{
	struct {
		struct gpu_header header;
		uint32 resource_id, nr_entries;
		uint64 address;
		uint32 length, padding;
	} request = {header(0x106), id, 1, (uint64)memory, length, 0};
	struct gpu_header response;
	gpu_request(&request, sizeof(request), &response, sizeof(response),
		    GPU_OK_NODATA);
}

static void transfer(struct gpu_rect rectangle, uint32 id)
{
	struct {
		struct gpu_header header;
		struct gpu_rect rectangle;
		uint64 offset;
		uint32 resource_id, padding;
	} request = {header(0x105), rectangle, 0, id, 0};
	struct gpu_header response;
	gpu_request(&request, sizeof(request), &response, sizeof(response),
		    GPU_OK_NODATA);
}

void gpu_raw_init(void)
{
	/* Both queues and their DMA memory remain owned by this global device. */
	vmmio_init(&gpu_driver.device, GPU_BASE, 16);
	vq_init(&gpu_driver.device, &gpu_driver.control_queue, 0);
	vq_init(&gpu_driver.device, &gpu_driver.cursor_queue, 1);
	vmmio_ready(&gpu_driver.device);
}

unsigned char *gpu_raw_setup_framebuffer(uint64 *length)
{
	struct gpu_header request = header(0x100);
	struct gpu_display_info response;
	gpu_request(&request, sizeof(request), &response, sizeof(response),
		    GPU_OK_DISPLAY_INFO);
	struct gpu_scanout scanout = response.scanouts[0];
	if (!scanout.enabled || !scanout.rect.width || !scanout.rect.height)
		panic("GPU scanout 0 is disabled");
	if (scanout.rect.width > 1920 || scanout.rect.height > 1080)
		panic("GPU mode exceeds provided DMA allocation");
	uint64 bytes = (uint64)scanout.rect.width * scanout.rect.height * 4;
	if (bytes > sizeof(framebuffer))
		panic("GPU framebuffer exceeds provided DMA allocation");
	gpu_driver.width = scanout.rect.width;
	gpu_driver.height = scanout.rect.height;
	gpu_driver.framebuffer = framebuffer;
	gpu_driver.length = PGROUNDUP(bytes);
	memset(framebuffer, 0, gpu_driver.length);
	resource_create(GPU_RESOURCE_FB, gpu_driver.width, gpu_driver.height);
	resource_attach(GPU_RESOURCE_FB, framebuffer, bytes);
	struct {
		struct gpu_header header;
		struct gpu_rect rectangle;
		uint32 scanout_id, resource_id;
	} set_scanout = {header(0x103), {0, 0, gpu_driver.width,
				      gpu_driver.height}, 0, GPU_RESOURCE_FB};
	struct gpu_header result;
	gpu_request(&set_scanout, sizeof(set_scanout), &result, sizeof(result),
		    GPU_OK_NODATA);
	*length = gpu_driver.length;
	printf("GPU framebuffer: %dx%d, %p, %d bytes\n", gpu_driver.width,
	       gpu_driver.height, (uint64)framebuffer, (int)gpu_driver.length);
	return framebuffer;
}

void gpu_raw_setup_cursor(void)
{
	/* Same 64x64 mouse.bmp bytes and transparency as rCore setup_cursor(). */
	for (uint32 i = 0; i < 64 * 64; ++i) {
		const unsigned char *color = cursor_palette[cursor_pixels[i]];
		cursor_buffer[i * 4] = color[0];
		cursor_buffer[i * 4 + 1] = color[1];
		cursor_buffer[i * 4 + 2] = color[2];
		cursor_buffer[i * 4 + 3] =
			(color[0] == 255 && color[1] == 255 && color[2] == 255)
			? 0 : 255;
	}
	resource_create(GPU_RESOURCE_CURSOR, 64, 64);
	resource_attach(GPU_RESOURCE_CURSOR, cursor_buffer, sizeof(cursor_buffer));
	transfer((struct gpu_rect){0, 0, 64, 64}, GPU_RESOURCE_CURSOR);
	struct {
		struct gpu_header header;
		uint32 scanout_id, x, y, padding;
		uint32 resource_id, hot_x, hot_y, padding2;
	} request = {header(0x300), 0, 50, 50, 0,
		     GPU_RESOURCE_CURSOR, 50, 50, 0};
	struct vq_buf buffer = {&request, sizeof(request), 0};
	vq_request(&gpu_driver.cursor_queue, &buffer, 1);
}

void gpu_raw_flush(void)
{
	struct gpu_rect rectangle = {0, 0, gpu_driver.width, gpu_driver.height};
	transfer(rectangle, GPU_RESOURCE_FB);
	struct {
		struct gpu_header header;
		struct gpu_rect rectangle;
		uint32 resource_id, padding;
	} request = {header(0x104), rectangle, GPU_RESOURCE_FB, 0};
	struct gpu_header response;
	gpu_request(&request, sizeof(request), &response, sizeof(response),
		    GPU_OK_NODATA);
}

void gpu_raw_ack(void)
{
	vmmio_ack(&gpu_driver.device);
}
