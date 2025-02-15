#ifndef PROC_H
#define PROC_H

#include "riscv.h"
#include "types.h"

#define NPROC (16)

#define MAX_SYSCALL_NUM 500 

#define VMA_MAX 16

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

// Per-process state
struct proc {
	enum procstate state; // Process state
	int pid; // Process ID
	pagetable_t pagetable; // User page table
	uint64 ustack;
	uint64 kstack; // Virtual address of kernel stack
	struct trapframe *trapframe; // data page for trampoline.S
	struct context context; // swtch() here to run process
	uint64 max_page;
	uint64 program_brk;
	uint64 heap_bottom;
	/*
	* LAB1: you may need to add some new fields here
	*/
	int time;
	int syscall_times[MAX_SYSCALL_NUM];
	// mmap
	struct VMA vma[VMA_MAX];
};

/*
* LAB1: you may need to define struct for TaskInfo here
*/

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
struct proc *allocproc();
// swtch.S
void swtch(struct context *, struct context *);

int growproc(int n);

#endif // PROC_H
