#ifndef PROC_H
#define PROC_H

#include "riscv.h"
#include "types.h"
#include "queue.h"

#define NPROC (512)
#define FD_BUFFER_SIZE (16)

struct file;

#define MAX_SYSCALL_NUM 500 

#define VMA_MAX 16

#define BIG_STRIDE 0x100000000ULL  // 2^32

// Saved registers for kernel context switches.
struct context {
	uint64 ra;
	uint64 sp;

	// callee-saved
	uint64 s0;
	uint64 s1;
	uint64 s2;
	uint64 s3;
	uint64 s4;
	uint64 s5;
	uint64 s6;
	uint64 s7;
	uint64 s8;
	uint64 s9;
	uint64 s10;
	uint64 s11;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };


 struct VMA{
   int valid;        //有效位，当值为 0 时表示无效，即为 empty element
   uint64 addr;      //记录起始地址
   int len;          //长度
   int prot;         //权限（read/write）
   int flags;        //区域类型（shared/private）
   int off;          //偏移量
//    struct file* f;   //映射的文件
   uint64 mapcnt;    //（延迟申请）已经映射的页数量
 };

/**
 * @brief 进程控制块结构体，包含进程的所有状态信息
 * @note 这个结构体是操作系统中进程管理的核心数据结构
 */
struct proc {
	enum procstate state;        // 进程状态（UNUSED/USED/SLEEPING等）
	int pid;                     // 进程ID，用于唯一标识进程
	pagetable_t pagetable;      // 用户进程页表，管理进程的虚拟地址空间
	uint64 ustack;              // 用户栈的虚拟地址
	uint64 kstack;              // 内核栈的虚拟地址
	struct trapframe *trapframe; // 保存进程切换时的寄存器状态
	struct context context;      // 进程上下文，用于进程切换
	uint64 max_page;            // 进程可使用的最大页数
	struct proc *parent;        // 父进程指针，用于进程树管理
	uint64 exit_code;           // 进程退出码
	struct file *files[FD_BUFFER_SIZE];  // 进程打开的文件描述符数组
	uint64 program_brk;         // 程序break位置，用于堆管理
	uint64 heap_bottom;         // 堆的起始地址
	int time;                   // 进程运行时间
	int syscall_times[MAX_SYSCALL_NUM];  // 系统调用次数统计数组
	
	// 内存映射区域管理
	struct VMA vma[VMA_MAX];    // 虚拟内存区域数组，用于mmap实现

	// 优先级
	uint64 stride;    // 当前步长
	uint64 pass;      // 步长增量
	long long priority;  // 优先级
};

int cpuid();
typedef enum {
    UnInit,
    Ready,
    Running,
    Exited,
} TaskStatus;

typedef struct {
    TaskStatus status;
    unsigned int syscall_times[MAX_SYSCALL_NUM];
    int time;
} TaskInfo;

struct proc *curr_proc();
void exit(int);
void proc_init();
void scheduler() __attribute__((noreturn));
void sched();
void yield();
int spawn(char *name);
int fork();
int exec(char *);
int wait(int, int *);
void add_task(struct proc *);
struct proc *pop_task();
struct proc *allocproc();
int fdalloc(struct file *);
// swtch.S
void swtch(struct context *, struct context *);

int growproc(int n);

#endif // PROC_H
