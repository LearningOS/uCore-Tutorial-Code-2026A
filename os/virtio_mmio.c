#include "defs.h"
#include "io.h"
#include "virtio_mmio.h"

#define MMIO_MAGIC 0x000
#define MMIO_VERSION 0x004
#define MMIO_DEVICE 0x008
#define MMIO_DEVICE_FEATURES 0x010
#define MMIO_DEVICE_FEATURES_SEL 0x014
#define MMIO_DRIVER_FEATURES 0x020
#define MMIO_DRIVER_FEATURES_SEL 0x024
#define MMIO_GUEST_PAGE_SIZE 0x028
#define MMIO_QUEUE_SEL 0x030
#define MMIO_QUEUE_NUM_MAX 0x034
#define MMIO_QUEUE_NUM 0x038
#define MMIO_QUEUE_ALIGN 0x03c
#define MMIO_QUEUE_PFN 0x040
#define MMIO_QUEUE_READY 0x044
#define MMIO_QUEUE_NOTIFY 0x050
#define MMIO_INTERRUPT_STATUS 0x060
#define MMIO_INTERRUPT_ACK 0x064
#define MMIO_STATUS 0x070
#define MMIO_QUEUE_DESC_LOW 0x080
#define MMIO_QUEUE_DRIVER_LOW 0x090
#define MMIO_QUEUE_DEVICE_LOW 0x0a0
#define STATUS_ACKNOWLEDGE 1
#define STATUS_DRIVER 2
#define STATUS_DRIVER_OK 4
#define STATUS_FEATURES_OK 8
#define DESC_NEXT 1
#define DESC_WRITE 2

static void dma_fence(void)
{
	asm volatile("fence iorw, iorw" ::: "memory");
}

uint32 vmmio_read(struct vmmio_device *dev, uint32 offset)
{
	return *(volatile uint32 *)(dev->base + offset);
}

void vmmio_write(struct vmmio_device *dev, uint32 offset, uint32 value)
{
	*(volatile uint32 *)(dev->base + offset) = value;
}

static void write_address(struct vmmio_device *dev, uint32 offset, uint64 addr)
{
	vmmio_write(dev, offset, (uint32)addr);
	vmmio_write(dev, offset + 4, (uint32)(addr >> 32));
}

void vmmio_init(struct vmmio_device *dev, uint64 base, uint32 device_id)
{
	dev->base = base;
	dev->version = vmmio_read(dev, MMIO_VERSION);
	if (vmmio_read(dev, MMIO_MAGIC) != 0x74726976 ||
	    (dev->version != 1 && dev->version != 2) ||
	    vmmio_read(dev, MMIO_DEVICE) != device_id)
		panic("VirtIO MMIO device %d missing at %p", device_id, base);

	vmmio_write(dev, MMIO_STATUS, 0);
	dma_fence();
	vmmio_write(dev, MMIO_STATUS, STATUS_ACKNOWLEDGE | STATUS_DRIVER);
	/* Neither event-index nor indirect descriptors are needed. Modern
	 * devices must advertise and accept VIRTIO_F_VERSION_1 (bit 32). */
	vmmio_write(dev, MMIO_DRIVER_FEATURES_SEL, 0);
	vmmio_write(dev, MMIO_DRIVER_FEATURES, 0);
	if (dev->version == 2) {
		vmmio_write(dev, MMIO_DEVICE_FEATURES_SEL, 1);
		if (!(vmmio_read(dev, MMIO_DEVICE_FEATURES) & 1))
			panic("VirtIO MMIO missing VERSION_1");
		vmmio_write(dev, MMIO_DRIVER_FEATURES_SEL, 1);
		vmmio_write(dev, MMIO_DRIVER_FEATURES, 1);
		vmmio_write(dev, MMIO_DEVICE_FEATURES_SEL, 0);
	}
	vmmio_write(dev, MMIO_STATUS,
		    STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK);
	if (!(vmmio_read(dev, MMIO_STATUS) & STATUS_FEATURES_OK))
		panic("VirtIO feature negotiation failed");
	if (dev->version == 1)
		vmmio_write(dev, MMIO_GUEST_PAGE_SIZE, VMMIO_PAGE_SIZE);
}

void vmmio_ready(struct vmmio_device *dev)
{
	dma_fence();
	vmmio_write(dev, MMIO_STATUS, vmmio_read(dev, MMIO_STATUS) |
		    STATUS_DRIVER_OK);
}

void vmmio_ack(struct vmmio_device *dev)
{
	uint32 pending = vmmio_read(dev, MMIO_INTERRUPT_STATUS);
	if (pending)
		vmmio_write(dev, MMIO_INTERRUPT_ACK, pending & 3);
	dma_fence();
}

void vq_init(struct vmmio_device *dev, struct virtqueue *q, uint16 index)
{
	memset(q, 0, sizeof(*q));
	q->dev = dev;
	q->index = index;
	q->desc = (struct vq_desc *)q->dma;
	q->avail = (struct vq_avail *)(q->dma + VQ_NUM * sizeof(struct vq_desc));
	q->used = (struct vq_used *)(q->dma + VMMIO_PAGE_SIZE);
	for (int i = 0; i < VQ_NUM; ++i)
		q->free[i] = 1;
	vmmio_write(dev, MMIO_QUEUE_SEL, index);
	if (vmmio_read(dev, MMIO_QUEUE_NUM_MAX) < VQ_NUM)
		panic("VirtIO queue %d too small", index);
	vmmio_write(dev, MMIO_QUEUE_NUM, VQ_NUM);
	if (dev->version == 1) {
		vmmio_write(dev, MMIO_QUEUE_ALIGN, VMMIO_PAGE_SIZE);
		vmmio_write(dev, MMIO_QUEUE_PFN, (uint64)q->dma >> 12);
	} else {
		write_address(dev, MMIO_QUEUE_DESC_LOW, (uint64)q->desc);
		write_address(dev, MMIO_QUEUE_DRIVER_LOW, (uint64)q->avail);
		write_address(dev, MMIO_QUEUE_DEVICE_LOW, (uint64)q->used);
		vmmio_write(dev, MMIO_QUEUE_READY, 1);
	}
}

int vq_next_token(struct virtqueue *q)
{
	for (int i = 0; i < VQ_NUM; ++i)
		if (q->free[i])
			return i;
	return -1;
}

int vq_submit(struct virtqueue *q, const struct vq_buf *bufs, int nbufs)
{
	int slots[VQ_NUM];
	if (nbufs < 1 || nbufs > VQ_NUM)
		panic("VirtIO invalid descriptor count");
	int found = 0;
	for (int i = 0; i < VQ_NUM && found < nbufs; ++i) {
		if (q->free[i])
			slots[found++] = i;
	}
	if (found != nbufs)
		return -1;
	for (int i = 0; i < nbufs; ++i) {
		int slot = slots[i];
		q->free[slot] = 0;
		q->desc[slot].addr = (uint64)bufs[i].addr;
		q->desc[slot].len = bufs[i].len;
		q->desc[slot].flags = bufs[i].write ? DESC_WRITE : 0;
		q->desc[slot].next = 0;
		if (i + 1 < nbufs) {
			q->desc[slot].flags |= DESC_NEXT;
			q->desc[slot].next = slots[i + 1];
		}
	}
	q->avail->ring[q->avail->idx % VQ_NUM] = slots[0];
	dma_fence();
	q->avail->idx++;
	dma_fence();
	vmmio_write(q->dev, MMIO_QUEUE_NOTIFY, q->index);
	return slots[0];
}

int vq_pop_used(struct virtqueue *q)
{
	if (q->used_idx == q->used->idx)
		return -1;
	dma_fence();
	uint32 head = q->used->ring[q->used_idx % VQ_NUM].id;
	if (head >= VQ_NUM || q->free[head])
		panic("VirtIO invalid completed token");
	int slot = head;
	for (int count = 0; count < VQ_NUM; ++count) {
		if (slot >= VQ_NUM || q->free[slot])
			panic("VirtIO invalid descriptor chain");
		int next = q->desc[slot].next;
		int chained = q->desc[slot].flags & DESC_NEXT;
		q->free[slot] = 1;
		memset(&q->desc[slot], 0, sizeof(q->desc[slot]));
		if (!chained) {
			q->used_idx++;
			return head;
		}
		slot = next;
	}
	panic("VirtIO cyclic descriptor chain");
}

int vq_request(struct virtqueue *q, const struct vq_buf *bufs, int nbufs)
{
	irq_state_t irq = io_irq_save();
	int token = vq_submit(q, bufs, nbufs);
	if (token < 0)
		panic("VirtIO synchronous queue full");
	int done;
	while ((done = vq_pop_used(q)) < 0)
		asm volatile("nop" ::: "memory");
	if (done != token)
		panic("VirtIO unexpected synchronous completion");
	vmmio_ack(q->dev);
	io_irq_restore(irq);
	return token;
}
