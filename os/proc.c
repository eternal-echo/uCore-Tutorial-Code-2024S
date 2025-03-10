#include "proc.h"
#include "defs.h"
#include "loader.h"
#include "trap.h"
#include "vm.h"
#include "timer.h"
#include "queue.h"

#define DEFAULT_PRIORITY 16        // 默认优先级

struct proc pool[NPROC];
__attribute__((aligned(16))) char kstack[NPROC][PAGE_SIZE];
__attribute__((aligned(4096))) char trapframe[NPROC][TRAP_PAGE_SIZE];

extern char boot_stack_top[];
struct proc *current_proc;
struct proc idle;
// struct queue task_queue;

int threadid()
{
	return curr_proc()->pid;
}

struct proc *curr_proc()
{
	return current_proc;
}

// initialize the proc table at boot time.
void proc_init()
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
	idle.pid = IDLE_PID;
	current_proc = &idle;
	// init_queue(&task_queue);
}

int allocpid()
{
	static int PID = 1;
	return PID++;
}

struct proc *fetch_task()
{
	int index = 0;
    uint64 min_stride = 0xFFFFFFFFFFFFFFFF;  // 初始设为最大值

	// int index = pop_queue(&task_queue);
	for (int i = 0; i < NPROC; i++) {
		if (pool[i].state == RUNNABLE) {
			if (pool[i].stride < min_stride) {
				min_stride = pool[i].stride;
				index = i;
			}
		}
	}
	if (index < 0) {
		debugf("No task to fetch\n");
		return NULL;
	}
	debugf("fetch task %d(pid=%d) to task queue\n", index, pool[index].pid);
	return pool + index;
}

void add_task(struct proc *p)
{
	// push_queue(&task_queue, p - pool);
	debugf("add task %d(pid=%d) to task queue\n", p - pool, p->pid);
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel.
// If there are no free procs, or a memory allocation fails, return 0.
struct proc *allocproc()
{
	struct proc *p;
	for (p = pool; p < &pool[NPROC]; p++) {
		if (p->state == UNUSED) {
			goto found;
		}
	}
	return 0;

found:
	// init proc
	p->pid = allocpid();
	p->state = USED;
	p->ustack = 0;
	p->max_page = 0;
	p->parent = NULL;
	p->exit_code = 0;
	p->pagetable = uvmcreate((uint64)p->trapframe);
	p->program_brk = 0;
        p->heap_bottom = 0;
	memset(&p->context, 0, sizeof(p->context));
	memset((void *)p->kstack, 0, KSTACK_SIZE);
	memset((void *)p->trapframe, 0, TRAP_PAGE_SIZE);
	p->context.ra = (uint64)usertrapret;
	p->context.sp = p->kstack + KSTACK_SIZE;

	// init vma
	for (int i = 0; i < VMA_MAX; i++) {
		p->vma[i].valid = 0;
		p->vma[i].mapcnt = 0;
	}

	// 在进程初始化时设置默认值
	p->stride = 0;
	p->priority = DEFAULT_PRIORITY;
	p->pass = BIG_STRIDE / DEFAULT_PRIORITY;

	return p;
}

// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler()
{
    struct proc *p;

	for (;;) {
		/*int has_proc = 0;
		for (p = pool; p < &pool[NPROC]; p++) {
			if (p->state == RUNNABLE) {
				has_proc = 1;
				tracef("swtich to proc %d", p - pool);
				
				p->state = RUNNING;
				current_proc = p;
				swtch(&idle.context, &p->context);
			}
		}
		if(has_proc == 0) {
			panic("all app are over!\n");
		}*/

		// 寻找stride最小的可运行进程
		p = fetch_task();
		if (p == NULL) {
			panic("all app are over!\n");
		}
		tracef("swtich to proc %d", p - pool);

		// 如果找到可运行进程
		if (p != NULL) {
			// 更新进程的 stride 值
			p->stride += p->pass;

			// 记录起始时间
			if (p->time == -1) {
				uint64 cycle = get_cycle();
				p->time = (int) ((cycle % CPU_FREQ) * 1000 / CPU_FREQ);
			}
			
			// 运行该进程
			p->state = RUNNING;
			current_proc = p;
			swtch(&idle.context, &p->context);
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
void sched()
{
	struct proc *p = curr_proc();
	if (p->state == RUNNING)
		panic("sched running");
	swtch(&p->context, &idle.context);
}

// Give up the CPU for one scheduling round.
void yield()
{
	current_proc->state = RUNNABLE;
	add_task(current_proc);
	sched();
}

// Free a process's page table, and free the
// physical memory it refers to.
void freepagetable(pagetable_t pagetable, uint64 max_page)
{
	uvmunmap(pagetable, TRAMPOLINE, 1, 0);
	uvmunmap(pagetable, TRAPFRAME, 1, 0);
	uvmfree(pagetable, max_page);
}

void freeproc(struct proc *p)
{
	if (p->pagetable)
		freepagetable(p->pagetable, p->max_page);
	p->pagetable = 0;
	p->state = UNUSED;
}

int spawn(char *name)
{
	// 获取父进程
	struct proc *p = curr_proc();
	// 分配一个新的进程控制块PCB
	struct proc *np = allocproc();
	if (np == 0) {
		panic("allocproc\n");
	}
	
	// 加载程序到进程空间
	int id = get_id_by_name(name);
	if (id < 0) {
		return -1;
	}
	loader(id, np);
	
	// 设置父子进程关系
	np->parent = p;
	
	// 将子进程状态设置为就绪态
	np->state = RUNNABLE;

	// 将新进程添加到任务调度队列
	add_task(np);

	return np->pid;
}


/**
 * @brief 创建一个新进程作为当前进程的子进程
 * 
 * @return 对于父进程返回子进程的pid，对于子进程返回0；失败时panic
 * 
 * 主要步骤：
 * 1. 为子进程分配PCB和相关资源
 * 2. 复制父进程的用户空间内存到子进程
 * 3. 设置子进程的trapframe和寄存器状态
 * 4. 建立父子进程关系并将子进程加入调度队列
 */
int fork()
{
	// 获取当前进程（父进程）的PCB
	struct proc *p = curr_proc();
	struct proc *np;

	// 为子进程分配一个新的进程控制块
	if ((np = allocproc()) == 0) {
		panic("allocproc\n");
	}

	// 复制父进程的页表内容到子进程，包括用户空间的所有内存
	if (uvmcopy(p->pagetable, np->pagetable, p->max_page) < 0) {
		panic("uvmcopy\n");
	}
	// 继承父进程的内存页数
	np->max_page = p->max_page;

	// 复制父进程的trapframe到子进程，这包含了进程的寄存器状态
	*(np->trapframe) = *(p->trapframe);
	
	// 设置子进程trapframe中的a0寄存器为0
	// 这样当子进程从fork返回时将得到0，而父进程得到子进程的pid
	np->trapframe->a0 = 0;

	// 设置父子进程关系
	np->parent = p;
	
	// 将子进程状态设置为就绪态
	np->state = RUNNABLE;
	
	// 将子进程添加到任务调度队列
	add_task(np);

	// 父进程返回子进程的pid
	return np->pid;
}

/**
 * @brief 将当前进程替换为新的程序
 * 
 * @param name 要加载的程序名称
 * @return 成功返回0，失败返回-1
 * 
 * 主要步骤：
 * 1. 根据程序名称获取程序ID
 * 2. 清空当前进程的用户空间
 * 3. 加载新程序到当前进程
 */
int exec(char *name)
{
	// 通过程序名称获取对应的程序ID，如果不存在则返回错误
	int id = get_id_by_name(name);
	if (id < 0)
		return -1;

	// 获取当前进程PCB
	struct proc *p = curr_proc();

	// 解除当前进程用户空间的所有映射（第四个参数1表示同时释放物理内存）
	uvmunmap(p->pagetable, 0, p->max_page, 1);
	
	// 重置进程的页面数量
	p->max_page = 0;

	// 加载新程序到当前进程的地址空间
	// loader函数负责设置进程的代码段、数据段等
	loader(id, p);

	return 0;
}

int wait(int pid, int *code)
{
	struct proc *np;
	int havekids;
	struct proc *p = curr_proc();

	for (;;) {
		// Scan through table looking for exited children.
		havekids = 0;
		for (np = pool; np < &pool[NPROC]; np++) {
			if (np->state != UNUSED && np->parent == p &&
			    (pid <= 0 || np->pid == pid)) {
				havekids = 1;
				if (np->state == ZOMBIE) {
					// Found one.
					np->state = UNUSED;
					pid = np->pid;
					*code = np->exit_code;
					return pid;
				}
			}
		}
		if (!havekids) {
			return -1;
		}
		p->state = RUNNABLE;
		add_task(p);
		sched();
	}
}

// Exit the current process.
void exit(int code)
{
	struct proc *p = curr_proc();
	p->exit_code = code;
	debugf("proc %d exit with %d\n", p->pid, code);
	freeproc(p);
	if (p->parent != NULL) {
		// Parent should `wait`
		p->state = ZOMBIE;
	}
	// Set the `parent` of all children to NULL
	struct proc *np;
	for (np = pool; np < &pool[NPROC]; np++) {
		if (np->parent == p) {
			np->parent = NULL;
		}
	}
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
