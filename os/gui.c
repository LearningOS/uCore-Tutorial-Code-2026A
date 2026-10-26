#include "defs.h"
#include "gui.h"
#include "virtio_gpu.h"

uint64 gui_framebuffer(pagetable_t pagetable)
{
	uint64 size;
	unsigned char *fb = virtio_gpu_get_framebuffer(&size);
	if (!fb || !size)
		panic("GPU framebuffer unavailable");
	if (!walkaddr(pagetable, FRAMEBUFFER_VA) &&
	    mappages(pagetable, FRAMEBUFFER_VA, PGROUNDUP(size), (uint64)fb,
		     PTE_U | PTE_R | PTE_W) < 0)
		panic("GPU user mapping failed");
	sfence_vma();
	return FRAMEBUFFER_VA;
}

void gui_unmap(pagetable_t pagetable)
{
	if (walkaddr(pagetable, FRAMEBUFFER_VA))
		uvmunmap(pagetable, FRAMEBUFFER_VA,
			 PGROUNDUP(gpu_framebuffer_size()) / PGSIZE, 0);
}

void gui_clone(pagetable_t old, pagetable_t new)
{
	if (walkaddr(old, FRAMEBUFFER_VA))
		gui_framebuffer(new);
}
