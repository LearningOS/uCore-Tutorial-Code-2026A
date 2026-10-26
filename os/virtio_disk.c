/* Keep the supplied filesystem's 1024-byte cache-block interface separate
 * from the chapter's 512-byte BlockDevice interface. All actual I/O, including
 * boot reads, goes through the three assessable VirtIO block functions. */
#include "bio.h"
#include "virtio.h"
#include "virtio_blk.h"

void virtio_disk_init(void)
{
	virtio_block_init();
}

void virtio_disk_rw(struct buf *b, int write)
{
	for (unsigned int offset = 0; offset < BSIZE; offset += VIRTIO_BLOCK_SIZE) {
		uint64 sector = (uint64)b->blockno * (BSIZE / VIRTIO_BLOCK_SIZE) +
			offset / VIRTIO_BLOCK_SIZE;
		if (write)
			virtio_block_write_block(sector, b->data + offset);
		else
			virtio_block_read_block(sector, b->data + offset);
	}
}

void virtio_disk_intr(void)
{
	virtio_block_handle_irq();
}
