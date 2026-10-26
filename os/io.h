#ifndef IO_H
#define IO_H

#include "types.h"
#include "queue.h"

/* Single-hart device critical sections. The saved SIE bit makes these nestable. */
typedef uint64 irq_state_t;
irq_state_t io_irq_save(void);
void io_irq_restore(irq_state_t state);

struct io_cond {
	struct queue wait_queue;
	int data[128];
};

void io_cond_init(struct io_cond *cond);
/* Requires interrupts disabled; changes state but does not switch context. */
void io_wait_no_sched(struct io_cond *cond);
/* Makes at most one waiter runnable; never switches context. */
void io_signal(struct io_cond *cond);

/* Set once by main after boot-time device and executable loading has finished. */
extern int dev_non_blocking_access;

#endif
