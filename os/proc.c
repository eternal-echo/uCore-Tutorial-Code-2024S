#include "proc.h"
#include "defs.h"
#include "loader.h"
#include "trap.h"
#include "vm.h"
#include "timer.h"

struct proc pool[NPROC];
__attribute__((aligned(16))) char kstack[NPROC][PAGE_SIZE];
__attribute__((aligned(4096))) char trapframe[NPROC][TRAP_PAGE_SIZE];

extern char boot_stack_top[];
struct proc *current_proc;
struct proc idle;

int threadid()
{
	return curr_proc()->pid;
}

struct proc *curr_proc()
{
	return current_proc;
}

// initialize the proc table at boot time.
void proc_init(void)
{
	struct proc *p;
	for (p = pool; p < &pool[NPROC]; p++) {
		p->state = UNUSED;
		p->kstack = (uint64)kstack[p - pool];
		p->trapframe = (struct trapframe *)trapframe[p - pool];
		/*
		* LAB1: you may need to initialize your new fields of proc here
		*/
		p->time = -1;
		memset(p->syscall_times, 0, sizeof(p->syscall_times));
	}
	idle.kstack = (uint64)boot_stack_top;
	idle.pid = 0;
	current_proc = &idle;
}

int allocpid()
{
	static int PID = 1;
	return PID++;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel.
// If there are no free procs, or a memory allocation fails, return 0.
struct proc *allocproc(void)
{
	struct proc *p;
	for (p = pool; p < &pool[NPROC]; p++) {
		if (p->state == UNUSED) {
			goto found;
		}
	}
	return 0;

found:
	p->pid = allocpid();
	p->state = USED;
	p->pagetable = 0;
	p->ustack = 0;
	p->max_page = 0;
	p->program_brk = 0;
        p->heap_bottom = 0;
	memset(&p->context, 0, sizeof(p->context));
	memset((void *)p->kstack, 0, KSTACK_SIZE);
	memset((void *)p->trapframe, 0, TRAP_PAGE_SIZE);
	p->context.ra = (uint64)usertrapret;
	p->context.sp = p->kstack + KSTACK_SIZE;
	return p;
}

// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
	struct proc *p;
	for (;;) {
		for (p = pool; p < &pool[NPROC]; p++) {
			if (p->state == RUNNABLE) {
				/*
				* LAB1: you may need to init proc start time here
				*/
				/*
				* LAB1: you may need to init proc start time here
				*/
				if (p->time == -1) {
					uint64 cycle = get_cycle();
					p->time = (int) ((cycle % CPU_FREQ) * 1000 / CPU_FREQ);
				}
				p->state = RUNNING;
				current_proc = p;
				swtch(&idle.context, &p->context);
			}
		}
	}
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
	struct proc *p = curr_proc();
	if (p->state == RUNNING)
		panic("sched running");
	swtch(&p->context, &idle.context);
}

// Give up the CPU for one scheduling round.
void yield(void)
{
	current_proc->state = RUNNABLE;
	sched();
}

void freeproc(struct proc *p)
{
	p->state = UNUSED;
	// uvmfree(p->pagetable, p->max_page);
}

// Exit the current process.
void exit(int code)
{
	struct proc *p = curr_proc();
	infof("proc %d exit with %d", p->pid, code);
	freeproc(p);
	finished();
	sched();
}
/*
 * 增长或缩减用户进程的内存空间。uvmalloc分配新的虚拟内存空间及对应的物理内存空间，建立页表映射关系。
 * 
 * @param n: 需要增加或减少的字节数。正数表示增加内存，负数表示减少内存
 * @return: 成功返回0，失败返回-1
 * 
 * 功能说明:
 * 1. 获取当前进程的program_brk（堆区结束地址）
 * 2. 计算新的堆区大小（相对于堆底的偏移量）
 * 3. 如果新的堆区大小小于0，返回错误
 * 4. 如果是扩展内存(n>0):
 *    - 调用uvmalloc分配新的虚拟内存空间
 *    - 分配失败返回-1
 * 5. 如果是收缩内存(n<0):
 *    - 调用uvmdealloc释放多余的虚拟内存空间
 * 6. 更新进程的program_brk
 * 
 * 注意事项:
 * - 此函数通常用于实现用户空间的brk/sbrk系统调用
 * - 所有内存操作都在页表pagetable中进行
 * - 新分配的内存页面具有可写权限(PTE_W)
 */

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
	// 声明program_brk变量用于存储堆区结束地址
	uint64 program_brk;
	// 获取当前进程的PCB
	struct proc *p = curr_proc();
	// 获取当前堆区结束地址
	program_brk = p->program_brk;
	// 计算新的堆区大小（相对于堆底的偏移量）
	int new_brk = program_brk + n - p->heap_bottom;
	// 如果新的堆区大小小于0，表示收缩过多，返回错误
	if(new_brk < 0){
		return -1;
	}
	// 扩展内存空间
	if(n > 0){
		// 调用uvmalloc分配新的虚拟内存空间，设置为可写
		if((program_brk = uvmalloc(p->pagetable, program_brk, program_brk + n, PTE_W)) == 0) {
			return -1;
		}
	} 
	// 收缩内存空间
	else if(n < 0){
		// 调用uvmdealloc释放多余的虚拟内存空间，注意，这里的n是负数，所以old地址大于new地址
		program_brk = uvmdealloc(p->pagetable, program_brk, program_brk + n);
	}
	// 更新进程的堆区结束地址
	p->program_brk = program_brk;
	return 0;
}
