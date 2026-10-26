#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "types.h"

#define VIRTIO_BLOCK_BASE 0x10008000UL
#define VIRTIO_BLOCK_IRQ 8
#define VIRTIO_BLOCK_SIZE 512

/* The chapter uses the same phase switch as rCore: false during boot,
 * true immediately before the scheduler starts running tasks. */
extern int dev_non_blocking_access;

void virtio_block_init(void);
void virtio_block_read_block(uint64 block_id, unsigned char *buf);
void virtio_block_write_block(uint64 block_id, const unsigned char *buf);
void virtio_block_handle_irq(void);
uint64 virtio_block_capacity(void);

#endif
