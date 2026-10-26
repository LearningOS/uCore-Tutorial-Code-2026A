#include "proc.h"
#include "defs.h"
#include "loader.h"
#include "trap.h"
#include "vm.h"
#include "queue.h"
#include "io.h"
#include "gui.h"

struct proc pool[NPROC];
__attribute__((aligned(16))) char kstack[NPROC][NTHREAD][KSTACK_SIZE];
__attribute__((aligned(4096))) char trapframe[NPROC][NTHREAD][TRAP_PAGE_SIZE];

extern char boot_stack_top[];
struct thread *current_thread;
struct thread idle;
struct queue task_queue;

int procid()
{
	return curr_proc()->pid;
}

int threadid()
{
	return curr_thread()->tid;
}

int cpuid()
{
	return 0;
}

struct proc *curr_proc()
{
	return current_thread->process;
}

struct thread *curr_thread()
{
	return current_thread;
}

// initialize the proc table at boot time.
void proc_init()
{
	struct proc *p;
	for (p = pool; p < &pool[NPROC]; p++) {
		p->state = P_UNUSED;
		for (int tid = 0; tid < NTHREAD; ++tid) {
			struct thread *t = &p->threads[tid];
			t->state = T_UNUSED;
		}
	}
	idle.kstack = (uint64)boot_stack_top;
	current_thread = &idle;
	// for procid() and threadid()
	idle.process = pool;
	idle.tid = -1;
	init_queue(&task_queue, QUEUE_SIZE, process_queue_data);
}

int allocpid()
{
	static int PID = 1;
	return PID++;
}

int alloctid(const struct proc *process)
{
	for (int i = 0; i < NTHREAD; ++i) {
		if (process->threads[i].state == T_UNUSED)
			return i;
	}
	return -1;
}

// get task by unique task id
struct thread *id_to_task(int index)
{
	if (index < 0 || index >= NPROC * NTHREAD) {
		return NULL;
	}
	int pool_id = index / NTHREAD;
	int tid = index % NTHREAD;
	struct thread *t = &pool[pool_id].threads[tid];
	return t;
}

// ncode unique task id for each thread
int task_to_id(struct thread *t)
{
	int pool_id = t->process - pool;
	int task_id = pool_id * NTHREAD + t->tid;
	return task_id;
}

struct thread *fetch_task()
{
	irq_state_t state = io_irq_save();
	int index = pop_queue(&task_queue);
	io_irq_restore(state);
	struct thread *t = id_to_task(index);
	if (t == NULL) {
		debugf("No task to fetch\n");
		return t;
	}
	int tid = t->tid;
	int pid = t->process->pid;
	tracef("fetch index %d(pid=%d, tid=%d, addr=%p) from task queue", index,
	       pid, tid, (uint64)t);
	return t;
}

void add_task(struct thread *t)
{
	int task_id = task_to_id(t);
	int pid = t->process->pid;
	irq_state_t state = io_irq_save();
	push_queue(&task_queue, task_id);
	io_irq_restore(state);
	tracef("add index %d(pid=%d, tid=%d, addr=%p) to task queue", task_id,
	       pid, t->tid, (uint64)t);
}

/* Remove every occurrence, preserving FIFO order for all other waiters.
 * Called only with SIE clear; cancellation also removes ready-queue entries. */
static void queue_remove_task(struct queue *q, struct thread *t)
{
	int id = task_to_id(t);
	int count = q->empty ? 0 : (q->tail - q->front + q->size) % q->size;
	if (!q->empty && count == 0)
		count = q->size;
	for (int i = 0; i < count; ++i) {
		int item = pop_queue(q);
		if (item != id)
			push_queue(q, item);
	}
}

void task_wait_no_sched(struct queue *queue, int completion)
{
	if (intr_get())
		panic("wait registration requires interrupts disabled");
	struct thread *t = curr_thread();
	if (!t || t->tid < 0 || t->wait_queue)
		panic("invalid wait registration");
	push_queue(queue, task_to_id(t));
	t->wait_queue = queue;
	t->wait_completion = completion;
	t->state = SLEEPING;
}

void task_wake(struct thread *t)
{
	t->wait_queue = NULL;
	t->wait_completion = 0;
	t->state = RUNNABLE;
	add_task(t);
}

/* A canceled continuation retains its user mappings until every sibling has
 * left the kernel. It is never allowed to execute another user instruction. */
static void __attribute__((noreturn)) retire_current(void)
{
	io_irq_save();
	struct thread *t = curr_thread();
	struct proc *p = t->process;
	if (t->wait_queue)
		panic("retiring a registered waiter");
	t->in_syscall = 0;
	t->state = T_RETIRED;
	queue_remove_task(&task_queue, t);
	io_signal(&p->kernel_drain);
	sched();
	panic("retired continuation resumed");
}

void task_cancel_checkpoint(void)
{
	struct thread *t = curr_thread();
	if (t && t->tid > 0 && t->process->dying)
		retire_current();
}

void task_syscall_enter(void)
{
	task_cancel_checkpoint();
	curr_thread()->in_syscall = 1;
}

void task_syscall_leave(void)
{
	curr_thread()->in_syscall = 0;
	task_cancel_checkpoint();
}

static void require_single_thread(struct proc *p)
{
	int count = 0;
	for (int i = 0; i < NTHREAD; ++i)
		if (p->threads[i].state != T_UNUSED)
			count++;
	if (count != 1 || curr_thread()->tid != 0)
		panic("fork/exec require one main thread, as in rCore ch9");
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel.
// If there are no free procs, or a memory allocation fails, return 0.
struct proc *allocproc()
{
	struct proc *p;
	for (p = pool; p < &pool[NPROC]; p++) {
		if (p->state == P_UNUSED) {
			goto found;
		}
	}
	return 0;

found:
	// init proc
	p->pid = allocpid();
	p->state = P_USED;
	p->max_page = 0;
	p->parent = NULL;
	p->exit_code = 0;
	p->dying = 0;
	io_cond_init(&p->kernel_drain);
	p->pagetable = uvmcreate();
	memset((void *)p->files, 0, sizeof(struct file *) * FD_BUFFER_SIZE);
	p->next_mutex_id = 0;
	p->next_semaphore_id = 0;
	p->next_condvar_id = 0;
	// LAB5: (1) you may initialize your new proc variables here
	return p;
}

inline uint64 get_thread_trapframe_va(int tid)
{
	return TRAPFRAME - tid * TRAP_PAGE_SIZE;
}

inline uint64 get_thread_ustack_base_va(struct thread *t)
{
	return t->process->ustack_base + t->tid * USTACK_SIZE;
}

int allocthread(struct proc *p, uint64 entry, int alloc_user_res)
{
	int tid;
	struct thread *t;
	for (tid = 0; tid < NTHREAD; ++tid) {
		t = &p->threads[tid];
		if (t->state == T_UNUSED ||
		    (t == curr_thread() && t->exec_rebuild)) {
			goto found;
		}
	}
	return -1;

found:
	t->tid = tid;
	t->state = T_USED;
	t->process = p;
	t->exit_code = 0;
	if (!t->exec_rebuild)
		t->in_syscall = 0;
	t->exec_rebuild = 0;
	t->wait_queue = NULL;
	t->wait_completion = 0;
	// kernel stack
	t->kstack = (uint64)kstack[p - pool][tid];
	// don't clear kstack now for exec()
	// memset((void *)t->kstack, 0, KSTACK_SIZE);
	// user stack
	t->ustack = get_thread_ustack_base_va(t);
	if (alloc_user_res != 0) {
		if (uvmmap(p->pagetable, t->ustack, USTACK_SIZE / PAGE_SIZE,
			   PTE_U | PTE_R | PTE_W) < 0) {
			panic("map ustack fail");
		}
		p->max_page =
			MAX(p->max_page,
			    PGROUNDUP(t->ustack + USTACK_SIZE - 1) / PAGE_SIZE);
	}
	// trap frame
	t->trapframe = (struct trapframe *)trapframe[p - pool][tid];
	memset((void *)t->trapframe, 0, TRAP_PAGE_SIZE);
	if (mappages(p->pagetable, get_thread_trapframe_va(tid), TRAP_PAGE_SIZE,
		     (uint64)t->trapframe, PTE_R | PTE_W) < 0) {
		panic("map trapframe fail");
	}
	t->trapframe->sp = t->ustack + USTACK_SIZE;
	t->trapframe->epc = entry;
	//task context
	memset(&t->context, 0, sizeof(t->context));
	t->context.ra = (uint64)usertrapret;
	t->context.sp = t->kstack + KSTACK_SIZE;
	// we do not add thread to scheduler immediately
	debugf("allocthread p: %d, o: %d, t: %d, e: %p, sp: %p, spp: %p",
	       p->pid, (p - pool), t->tid, entry, t->ustack,
	       useraddr(p->pagetable, t->ustack));
	return tid;
}

int init_stdio(struct proc *p)
{
	for (int i = 0; i < 3; i++) {
		if (p->files[i] != NULL) {
			return -1;
		}
		p->files[i] = stdio_init(i);
	}
	return 0;
}

// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
/* There can be no runnable thread while an I/O completion is pending. */
static int live_threads(void)
{
	for (int p = 0; p < NPROC; ++p)
		for (int tid = 0; tid < NTHREAD; ++tid) {
			enum threadstate s = pool[p].threads[tid].state;
			if (s == RUNNING || s == RUNNABLE || s == SLEEPING || s == T_USED)
				return 1;
		}
	return 0;
}

void scheduler()
{
	for (;;) {
		intr_off();
		current_thread = &idle;
		struct thread *t = fetch_task();
		if (!t) {
			if (!live_threads()) {
				printf("all app are over!\n");
				shutdown();
			}
			/* WFI observes locally enabled IRQs even with global SIE clear.
			 * An IRQ cannot be consumed between the empty check and WFI. */
			asm volatile("wfi" ::: "memory");
			intr_on();
			continue;
		}
		if (t->state != RUNNABLE)
			continue;
		t->state = RUNNING;
		current_thread = t;
		swtch(&idle.context, &t->context);
	}
}

/* The interrupt-enable state belongs to the suspended kernel continuation.
 * No device critical section may remain held across this call. */
void sched()
{
	irq_state_t state = io_irq_save();
	struct thread *t = curr_thread();
	if (t->state == RUNNING)
		panic("sched running");
	swtch(&t->context, &idle.context);
	io_irq_restore(state);
}

void yield()
{
	task_cancel_checkpoint();
	irq_state_t state = io_irq_save();
	current_thread->state = RUNNABLE;
	add_task(current_thread);
	sched();
	io_irq_restore(state);
}

// Free a process's page table, and free the
// physical memory it refers to.
void freepagetable(pagetable_t pagetable, uint64 max_page)
{
	uvmunmap(pagetable, TRAMPOLINE, 1, 0);
	uvmfree(pagetable, max_page);
}

void freethread(struct thread *t)
{
	pagetable_t pt = t->process->pagetable;
	// fill with junk
	memset((void *)t->trapframe, 6, TRAP_PAGE_SIZE);
	memset(&t->context, 6, sizeof(t->context));
	uvmunmap(pt, get_thread_trapframe_va(t->tid), 1, 0);
	uvmunmap(pt, get_thread_ustack_base_va(t), USTACK_SIZE / PAGE_SIZE, 1);
}

void freeproc(struct proc *p)
{
	for (int tid = 0; tid < NTHREAD; ++tid) {
		struct thread *t = &p->threads[tid];
		if (t->state != T_UNUSED && t->state != EXITED) {
			freethread(t);
		}
		t->state = T_UNUSED;
		t->in_syscall = 0;
		t->wait_queue = NULL;
		t->wait_completion = 0;
	}
	if (p->pagetable)
		freepagetable(p->pagetable, p->max_page);
	p->pagetable = 0;
	p->max_page = 0;
	p->ustack_base = 0;
	for (int i = 0; i < FD_BUFFER_SIZE; i++) {
		if (p->files[i] != NULL) {
			fileclose(p->files[i]);
			p->files[i] = NULL;
		}
	}
	p->state = P_UNUSED;
}

int fork()
{
	struct proc *np;
	struct proc *p = curr_proc();
	require_single_thread(p);
	int i;
	// Allocate process.
	if ((np = allocproc()) == 0) {
		panic("allocproc\n");
	}
	// Copy user memory from parent to child.
	if (uvmcopy(p->pagetable, np->pagetable, p->max_page) < 0) {
		panic("uvmcopy\n");
	}
	np->max_page = p->max_page;
	np->ustack_base = p->ustack_base;
	// Copy file table to new proc
	for (i = 0; i < FD_BUFFER_SIZE; i++) {
		if (p->files[i] != NULL) {
			// TODO: f->type == STDIO ?
			p->files[i]->ref++;
			np->files[i] = p->files[i];
		}
	}

	np->parent = p;
	// currently only copy main thread
	struct thread *nt = &np->threads[allocthread(np, 0, 0)],
		      *t = &p->threads[0];
	// copy saved user registers.
	*(nt->trapframe) = *(t->trapframe);
	// Cause fork to return 0 in the child.
	nt->trapframe->a0 = 0;
	nt->state = RUNNABLE;
	add_task(nt);
	return np->pid;
}

int push_argv(struct proc *p, char **argv)
{
	uint64 argc, ustack[MAX_ARG_NUM + 1];
	// only push to main thread
	struct thread *t = &p->threads[0];
	uint64 sp = t->ustack + USTACK_SIZE, spb = t->ustack;
	debugf("[push] sp: %p, spb: %p", sp, spb);
	// Push argument strings, prepare rest of stack in ustack.
	for (argc = 0; argv[argc]; argc++) {
		if (argc >= MAX_ARG_NUM)
			panic("too many args!");
		sp -= strlen(argv[argc]) + 1;
		sp -= sp % 16; // riscv sp must be 16-byte aligned
		if (sp < spb) {
			panic("uset stack overflow!");
		}
		if (copyout(p->pagetable, sp, argv[argc],
			    strlen(argv[argc]) + 1) < 0) {
			panic("copy argv failed!");
		}
		ustack[argc] = sp;
	}
	ustack[argc] = 0;
	// push the array of argv[] pointers.
	sp -= (argc + 1) * sizeof(uint64);
	sp -= sp % 16;
	if (sp < spb) {
		panic("uset stack overflow!");
	}
	if (copyout(p->pagetable, sp, (char *)ustack,
		    (argc + 1) * sizeof(uint64)) < 0) {
		panic("copy argc failed!");
	}
	t->trapframe->a1 = sp;
	t->trapframe->sp = sp;
	// clear files ?
	return argc; // this ends up in a0, the first argument to main(argc, argv)
}

int exec(char *path, char **argv)
{
	infof("exec : %s\n", path);
	struct inode *ip;
	struct proc *p = curr_proc();
	require_single_thread(p);
	if ((ip = namei(path)) == 0) {
		errorf("invalid file name %s\n", path);
		return -1;
	}
	// free current main thread's ustack and trapframe
	struct thread *t = curr_thread();
	freethread(t);
	t->state = RUNNING;
	t->exec_rebuild = 1;
	gui_unmap(p->pagetable);
	uvmunmap(p->pagetable, 0, p->max_page, 1);
	bin_loader(ip, p);
	iput(ip);
	t->state = RUNNING;
	return push_argv(p, argv);
}

int wait(int pid, int *code)
{
	struct proc *np;
	int havekids;
	struct proc *p = curr_proc();
	struct thread *t = curr_thread();

	for (;;) {
		task_cancel_checkpoint();
		// Scan through table looking for exited children.
		havekids = 0;
		for (np = pool; np < &pool[NPROC]; np++) {
			if (np->state != P_UNUSED && np->parent == p &&
			    (pid <= 0 || np->pid == pid)) {
				havekids = 1;
				if (np->state == ZOMBIE) {
					// Found one.
					np->state = P_UNUSED;
					pid = np->pid;
					*code = np->exit_code;
					memset((void *)np->threads[0].kstack, 9,
					       KSTACK_SIZE);
					return pid;
				}
			}
		}
		if (!havekids) {
			return -1;
		}
		t->state = RUNNABLE;
		add_task(t);
		sched();
	}
}

/* Stop user execution immediately. Kernel continuations that already own
 * device requests must finish before their stacks and mappings can be freed. */
static int sibling_kernel_active(struct proc *p)
{
	for (int i = 1; i < NTHREAD; ++i)
		if (p->threads[i].in_syscall)
			return 1;
	return 0;
}

void exit(int code)
{
	struct proc *p = curr_proc();
	struct thread *t = curr_thread();
	if (t->tid != 0) {
		t->exit_code = code;
		t->in_syscall = 0;
		if (p->dying)
			retire_current();
		t->state = EXITED;
		freethread(t);
		sched();
		panic("exited thread resumed");
	}

	irq_state_t state = io_irq_save();
	p->dying = 1;
	p->exit_code = code;
	t->in_syscall = 0;
	for (int i = 1; i < NTHREAD; ++i) {
		struct thread *other = &p->threads[i];
		if (other->state == T_UNUSED || other->state == EXITED ||
		    other->state == T_RETIRED)
			continue;
		if (!other->in_syscall) {
			queue_remove_task(&task_queue, other);
			other->state = T_RETIRED;
		} else if (other->wait_queue && !other->wait_completion) {
			/* UART, FS-lock and synchronization waits have no outstanding
			 * DMA. Wake them to reach a cancellation checkpoint. */
			queue_remove_task(other->wait_queue, other);
			task_wake(other);
		}
	}
	io_irq_restore(state);

	for (;;) {
		state = io_irq_save();
		if (!sibling_kernel_active(p)) {
			io_irq_restore(state);
			break;
		}
		io_wait_no_sched(&p->kernel_drain);
		io_irq_restore(state);
		sched();
	}
	/* No continuation or device request can now refer to this process's
	 * stack/user pages. Retired slots remain unavailable until this point. */
	t->exit_code = code;
	t->state = EXITED;
	freethread(t);
	freeproc(p);
	if (p->parent)
		p->state = ZOMBIE;
	for (struct proc *child = pool; child < &pool[NPROC]; ++child)
		if (child->parent == p)
			child->parent = NULL;
	sched();
	panic("exited process resumed");
}

int fdalloc(struct file *f)
{
	debugf("debugf f = %p, type = %d", f, f->type);
	struct proc *p = curr_proc();
	for (int i = 0; i < FD_BUFFER_SIZE; ++i) {
		if (p->files[i] == NULL) {
			p->files[i] = f;
			debugf("debugf fd = %d, f = %p", i, p->files[i]);
			return i;
		}
	}
	return -1;
}