#include "io.h"
#include "defs.h"

irq_state_t io_irq_save(void)
{
	uint64 old;
	/* An atomic read-and-clear avoids a pending IRQ between the two operations. */
	asm volatile("csrrc %0, sstatus, %1" : "=r"(old)
		     : "r"((uint64)SSTATUS_SIE) : "memory");
	return old & SSTATUS_SIE;
}

void io_irq_restore(irq_state_t state)
{
	asm volatile("" ::: "memory");
	if (state & SSTATUS_SIE)
		intr_on();
	else
		intr_off();
}

void io_cond_init(struct io_cond *cond)
{
	init_queue(&cond->wait_queue, NELEM(cond->data), cond->data);
}

void io_wait_no_sched(struct io_cond *cond)
{
	if (intr_get())
		panic("I/O waiter registration must be protected");
	struct thread *t = curr_thread();
	if (!t || t->tid < 0)
		panic("boot has no thread to block");
	push_queue(&cond->wait_queue, task_to_id(t));
	t->state = SLEEPING;
}

void io_signal(struct io_cond *cond)
{
	irq_state_t state = io_irq_save();
	struct thread *t = id_to_task(pop_queue(&cond->wait_queue));
	if (t) {
		if (t->state != SLEEPING)
			panic("I/O waiter is not sleeping");
		t->state = RUNNABLE;
		add_task(t);
	}
	io_irq_restore(state);
}
