#include "defs.h"
#include "io.h"
#include "virtio_blk.h"
#include "virtio_mmio.h"

struct blk_request {
	uint32 type;
	uint32 reserved;
	uint64 sector;
};

struct blk_response {
	volatile unsigned char status;
};

static struct {
	struct vmmio_device dev;
	struct virtqueue queue;
	struct blk_request requests[VQ_NUM];
	struct io_cond condvars[VQ_NUM];
	uint64 capacity;
} block;

void virtio_block_init(void)
{
	vmmio_init(&block.dev, VIRTIO_BLOCK_BASE, 2);
	vq_init(&block.dev, &block.queue, 0);
	for (int i = 0; i < VQ_NUM; ++i)
		io_cond_init(&block.condvars[i]);
	block.capacity = vmmio_read(&block.dev, 0x100) |
		((uint64)vmmio_read(&block.dev, 0x104) << 32);
	if (!block.capacity)
		panic("VirtIO block has no sectors");
	vmmio_ready(&block.dev);
}

uint64 virtio_block_capacity(void)
{
	return block.capacity;
}

/* Supplied nonblocking driver operation. The request headers are owned by
 * the device wrapper, while data and response remain owned by the caller.
 * Called with interrupts disabled, including the subsequent wait registration. */
static int block_submit(uint64 block_id, void *buf,
			struct blk_response *resp, int write)
{
	if (block_id >= block.capacity || !buf)
		panic("VirtIO block request out of range");
	int token = vq_next_token(&block.queue);
	if (token < 0)
		panic("VirtIO block queue full");
	struct blk_request *req = &block.requests[token];
	req->type = write ? 1 : 0;
	req->reserved = 0;
	req->sector = block_id;
	resp->status = 0xff;
	struct vq_buf bufs[3] = {
		{ req, sizeof(*req), 0 },
		{ buf, VIRTIO_BLOCK_SIZE, !write },
		{ (void *)&resp->status, sizeof(resp->status), 1 },
	};
	int head = vq_submit(&block.queue, bufs, 3);
	if (head != token)
		panic("VirtIO block submission failed");
	return token;
}

/* Supplied synchronous boot path: no task exists to register as a waiter.
 * Interrupts remain disabled while polling the used ring. */
static void __attribute__((unused))
block_sync(uint64 block_id, void *buf, int write)
{
	irq_state_t irq = io_irq_save();
	struct blk_response resp;
	int token = block_submit(block_id, buf, &resp, write);
	int done;
	while ((done = vq_pop_used(&block.queue)) < 0)
		asm volatile("nop" ::: "memory");
	if (done != token || resp.status != 0)
		panic("VirtIO block synchronous request failed");
	vmmio_ack(&block.dev);
	io_irq_restore(irq);
}

void virtio_block_read_block(uint64 block_id, unsigned char *buf)
{
	/* LAB9_BEGIN virtio_block_read_block */
	irq_state_t irq = io_irq_save();
	int nonblocking = dev_non_blocking_access;
	io_irq_restore(irq);
	if (!nonblocking) {
		block_sync(block_id, buf, 0);
		return;
	}
	struct blk_response resp = { .status = 0xff };
	irq = io_irq_save();
	int token = block_submit(block_id, buf, &resp, 0);
	io_wait_no_sched(&block.condvars[token]);
	io_irq_restore(irq);
	sched();
	if (resp.status != 0)
		panic("Error when reading VirtIOBlk");
	/* LAB9_END virtio_block_read_block */
}

void virtio_block_write_block(uint64 block_id, const unsigned char *buf)
{
	/* LAB9_BEGIN virtio_block_write_block */
	irq_state_t irq = io_irq_save();
	int nonblocking = dev_non_blocking_access;
	io_irq_restore(irq);
	if (!nonblocking) {
		block_sync(block_id, (void *)buf, 1);
		return;
	}
	struct blk_response resp = { .status = 0xff };
	irq = io_irq_save();
	int token = block_submit(block_id, (void *)buf, &resp, 1);
	io_wait_no_sched(&block.condvars[token]);
	io_irq_restore(irq);
	sched();
	if (resp.status != 0)
		panic("Error when writing VirtIOBlk");
	/* LAB9_END virtio_block_write_block */
}

void virtio_block_handle_irq(void)
{
	/* LAB9_BEGIN virtio_block_handle_irq */
	irq_state_t irq = io_irq_save();
	vmmio_ack(&block.dev);
	int token;
	while ((token = vq_pop_used(&block.queue)) >= 0)
		io_signal(&block.condvars[token]);
	io_irq_restore(irq);
	/* LAB9_END virtio_block_handle_irq */
}
