#include "plic.h"
#include "log.h"
#include "proc.h"
#include "riscv.h"
#include "types.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void plicinit()
{
	// set desired IRQ priorities non-zero (otherwise disabled).
	int hart = cpuid();
	uint32 enabled = 0;
	int irqs[] = { UART0_IRQ, VIRTIO0_IRQ, VIRTIO_GPU_IRQ,
		       VIRTIO_KEYBOARD_IRQ, VIRTIO_MOUSE_IRQ };
	for (uint i = 0; i < sizeof(irqs) / sizeof(irqs[0]); ++i) {
		*(volatile uint32 *)(PLIC + irqs[i] * 4) = 1;
		enabled |= 1U << irqs[i];
	}
	*(volatile uint32 *)PLIC_SENABLE(hart) = enabled;
	// set this hart's S-mode priority threshold to 0.
	*(uint32 *)PLIC_SPRIORITY(hart) = 0;
}

// ask the PLIC what interrupt we should serve.
int plic_claim()
{
	int hart = cpuid();
	int irq = *(uint32 *)PLIC_SCLAIM(hart);
	return irq;
}

// tell the PLIC we've served this IRQ.
void plic_complete(int irq)
{
	int hart = cpuid();
	*(uint32 *)PLIC_SCLAIM(hart) = irq;
}
