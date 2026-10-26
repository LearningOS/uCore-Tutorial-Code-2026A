#ifndef VIRTIO_MMIO_H
#define VIRTIO_MMIO_H

#include "types.h"

/* Supplied transport, analogous to virtio-drivers in the Rust chapter.
 * All DMA memory uses the kernel's identity mapping. Queue callers serialize
 * accesses by disabling interrupts on the chapter's single hart. */
#define VQ_NUM 32
#define VMMIO_PAGE_SIZE 4096

struct vmmio_device {
	uint64 base;
	uint32 version;
};

struct vq_desc {
	uint64 addr;
	uint32 len;
	uint16 flags;
	uint16 next;
};

struct vq_avail {
	uint16 flags;
	uint16 idx;
	uint16 ring[VQ_NUM];
	uint16 used_event;
};

struct vq_used_elem {
	uint32 id;
	uint32 len;
};

struct vq_used {
	uint16 flags;
	uint16 idx;
	struct vq_used_elem ring[VQ_NUM];
	uint16 avail_event;
};

struct virtqueue {
	/* Legacy queues require contiguous descriptors/avail/used pages. The
	 * modern MMIO transport uses these same pages through full addresses. */
	unsigned char dma[2 * VMMIO_PAGE_SIZE]
		__attribute__((aligned(VMMIO_PAGE_SIZE)));
	struct vq_desc *desc;
	volatile struct vq_avail *avail;
	volatile struct vq_used *used;
	struct vmmio_device *dev;
	uint16 index;
	uint16 used_idx;
	unsigned char free[VQ_NUM];
};

struct vq_buf {
	void *addr;
	uint32 len;
	int write; /* Device may write this buffer. */
};

uint32 vmmio_read(struct vmmio_device *, uint32 offset);
void vmmio_write(struct vmmio_device *, uint32 offset, uint32 value);
void vmmio_init(struct vmmio_device *, uint64 base, uint32 device_id);
void vmmio_ready(struct vmmio_device *);
void vmmio_ack(struct vmmio_device *);
void vq_init(struct vmmio_device *, struct virtqueue *, uint16 index);
int vq_next_token(struct virtqueue *);
int vq_submit(struct virtqueue *, const struct vq_buf *, int nbufs);
/* Returns a completed head token, reclaims its complete descriptor chain,
 * or returns -1 if no currently completed entry exists. It never waits. */
int vq_pop_used(struct virtqueue *);
/* Synchronous polling helper for boot/device control commands. No current
 * task is required. All commands on this queue must use this helper. */
int vq_request(struct virtqueue *, const struct vq_buf *, int nbufs);

#endif
