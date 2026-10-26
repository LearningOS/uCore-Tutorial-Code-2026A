#include "defs.h"
#include "proc.h"
#include "sync.h"
#include "io.h"

struct mutex *mutex_create(int blocking)
{
	struct proc *p = curr_proc();
	if (p->next_mutex_id >= LOCK_POOL_SIZE)
		return NULL;
	struct mutex *m = &p->mutex_pool[p->next_mutex_id++];
	m->blocking = blocking;
	m->locked = 0;
	if (blocking)
		init_queue(&m->wait_queue, WAIT_QUEUE_MAX_LENGTH,
			   m->_wait_queue_data);
	return m;
}

void mutex_lock(struct mutex *m)
{
	for (;;) {
		irq_state_t state = io_irq_save();
		task_cancel_checkpoint();
		if (!m->locked) {
			m->locked = 1;
			io_irq_restore(state);
			return;
		}
		if (!m->blocking) {
			io_irq_restore(state);
			yield();
			continue;
		}
		task_wait_no_sched(&m->wait_queue, 0);
		io_irq_restore(state);
		sched();
		task_cancel_checkpoint();
		/* unlock transfers ownership to the first blocked waiter. */
		return;
	}
}

void mutex_unlock(struct mutex *m)
{
	irq_state_t state = io_irq_save();
	struct thread *t = m->blocking ?
		id_to_task(pop_queue(&m->wait_queue)) : NULL;
	if (t)
		task_wake(t);
	else
		m->locked = 0;
	io_irq_restore(state);
}

struct semaphore *semaphore_create(int count)
{
	struct proc *p = curr_proc();
	if (p->next_semaphore_id >= LOCK_POOL_SIZE)
		return NULL;
	struct semaphore *s = &p->semaphore_pool[p->next_semaphore_id++];
	s->count = count;
	init_queue(&s->wait_queue, WAIT_QUEUE_MAX_LENGTH, s->_wait_queue_data);
	return s;
}

void semaphore_up(struct semaphore *s)
{
	irq_state_t state = io_irq_save();
	s->count++;
	if (s->count <= 0) {
		struct thread *t = id_to_task(pop_queue(&s->wait_queue));
		if (!t)
			panic("semaphore waiter missing");
		task_wake(t);
	}
	io_irq_restore(state);
}

void semaphore_down(struct semaphore *s)
{
	irq_state_t state = io_irq_save();
	task_cancel_checkpoint();
	s->count--;
	if (s->count < 0) {
		task_wait_no_sched(&s->wait_queue, 0);
		io_irq_restore(state);
		sched();
		task_cancel_checkpoint();
		return;
	}
	io_irq_restore(state);
}

struct condvar *condvar_create()
{
	struct proc *p = curr_proc();
	if (p->next_condvar_id >= LOCK_POOL_SIZE)
		return NULL;
	struct condvar *c = &p->condvar_pool[p->next_condvar_id++];
	init_queue(&c->wait_queue, WAIT_QUEUE_MAX_LENGTH, c->_wait_queue_data);
	return c;
}

void cond_signal(struct condvar *cond)
{
	irq_state_t state = io_irq_save();
	struct thread *t = id_to_task(pop_queue(&cond->wait_queue));
	if (t)
		task_wake(t);
	io_irq_restore(state);
}

void cond_wait(struct condvar *cond, struct mutex *m)
{
	irq_state_t state = io_irq_save();
	task_cancel_checkpoint();
	mutex_unlock(m);
	task_wait_no_sched(&cond->wait_queue, 0);
	io_irq_restore(state);
	sched();
	task_cancel_checkpoint();
	mutex_lock(m);
}
