#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "proc.h"
#include "kalloc.h"
#include "vm.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	// YOUR CODE
	uint64 cycle = get_cycle();
	TimeVal tv;
	tv.sec = cycle / CPU_FREQ;
	tv.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

	struct proc *p = curr_proc();
	if (copyout(p->pagetable, (uint64)val, (char *)&tv, sizeof(TimeVal)) < 0) {
		return -1;
	}
	// infof("sys_gettimeofday: sec = %d, usec = %d", tv.sec, tv.usec);
	// debugf("sec = %d, usec = %d", tv.sec, tv.usec);
	/* The code in `ch3` will leads to memory bugs*/

	// uint64 cycle = get_cycle();
	// val->sec = cycle / CPU_FREQ;
	// val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return 0;
}

/**
 * @brief 系统调用：调整程序的堆内存大小
 * 
 * sbrk()系统调用通过增加或减少n字节来调整程序的数据空间（堆空间）。
 * 主要用途：
 * 1. 为程序动态分配内存（如malloc的底层实现）
 * 2. 管理进程的堆空间大小
 * 
 * 关键点：
 * - program_brk是进程堆空间的当前末尾地址
 * - 通过growproc()来实际改变进程的内存空间
 * - 返回增长/收缩前的program_brk值
 * 
 * 堆空间管理：
 * - 增加空间(n > 0): growproc会分配新的物理页面并映射到虚拟地址空间
 * - 减少空间(n < 0): growproc会解除映射并释放相应的物理页面
 * - 空间来源：系统的空闲物理内存页面
 * 
 * @param n 需要调整的字节数（正数增加空间，负数减少空间）
 * @return uint64 调整前的program_brk地址（成功），-1（失败）
 */
uint64 sys_sbrk(int n)
{
	uint64 addr;
	struct proc *p = curr_proc();
	addr = p->program_brk;
	if(growproc(n) < 0) {
		return -1;
	}
	return addr;	
}



// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/**
 * @brief 将物理内存映射到虚拟地址空间
 * 
 * @param start 需要映射的虚存起始地址
 * @param len 映射字节长度，可以为 0，上限 1GiB
 * @param port 内存权限，第0位可读，第1位可写，第2位可执行
 * @param flag 目前始终为0，忽略
 * @param fd 目前始终为0，忽略
 * @return int 成功返回0，失败返回-1
 */
int sys_mmap(void* start, unsigned long long len, int port, int flag, int fd) {
    struct proc *p = curr_proc();
    
    // 基础参数检查
	if (!PGALIGNED((uint64)start) || len == 0) {
		errorf("sys_mmap: invalid start address or length");
        return -1;
    }
    
    // 检查长度上限(1GiB)
    if (len > (1ULL << 30)) {
		errorf("sys_mmap: invalid length");
        return -1;
    }
    
    // 检查权限位：0-7
    if ((port & ~0x7) != 0 || (port & 0x7) == 0) {
		errorf("sys_mmap: invalid port");
        return -1;
    }
    
	// 申请新的虚存空间并映射
    uint64 addr = (uint64)start;
	uint64 newsz = addr + (uint64)len;
    if (uvmalloc(p->pagetable, addr, newsz, port) == 0) {
		errorf("sys_mmap: 分配失败或映射失败（例如重复映射）");
		return -1;
	}

    
    return 0;
}

/**
 * @brief 取消一块虚存的映射
 * 
 * @param start 需要取消映射的虚存起始地址，必须页对齐
 * @param len 取消映射的字节长度
 * @return int 成功返回0，失败返回-1
 */
int sys_munmap(void* start, unsigned long long len) {
    struct proc *p = curr_proc();
    
    // 参数检查
    if (!PGALIGNED((uint64)start) || len == 0 || (len % PGSIZE != 0)) {
		errorf("sys_munmap: invalid start address or length");
        return -1;
    }
    
    // 向上取整为页大小
    uint64 size = PGROUNDUP(len);
    
    // 检查地址范围是否都已映射
    uint64 addr;
    addr = (uint64)start;
	if (useraddr(p->pagetable, addr) <= 0) {
		// 逐页解除映射并释放物理内存
		uvmunmap(p->pagetable, (uint64)start, size / PGSIZE, 1);
	}
    
    return 0;
}

/*
* LAB1: you may need to define sys_task_info here
*/

/**
 * @brief  sys_task_info 用于获取当前正在运行的任务（进程）的信息。系统调用功能为
 * 			查询当前任务的详细信息，包括：
 * 			- **任务状态**：当前任务的执行状态（如运行中、已退出等）。
 *			- **系统调用次数**：记录任务自启动以来调用每个**系统调用**的次数。
 *			- **运行时间**：当前时间与任务首次被调度的时间差（以毫秒为单位）。
 * 
 * @param ti TaskInfo 是一个结构体指针，用于存储返回的任务信息。
 * @return int 成功返回 0，失败返回 1。
 */
int sys_task_info(TaskInfo *ti) {
	// TODO：检查用户空间指针是否合法。ucore不支持。
	
	struct proc *p = curr_proc();
	if (p == 0) {
		return -1;
	}

    // 在内核空间创建临时结构体
    TaskInfo kernel_ti;
    kernel_ti.status = Running;

    // 复制系统调用次数
    for (int i = 0; i < MAX_SYSCALL_NUM; i++) {
        kernel_ti.syscall_times[i] = p->syscall_times[i];
    }

    // 计算运行时间
    int current_time = (int)((get_cycle() % CPU_FREQ) * 1000 / CPU_FREQ);
    kernel_ti.time = current_time - p->time;

    // 将数据从内核空间复制到用户空间
    if (copyout(p->pagetable, (uint64)ti, (char *)(&kernel_ti), sizeof(TaskInfo)) < 0) {
        return -1;
    }

	debugf("sys_task_info: current_time = %d, proc's time = %d", current_time, p->time);
	debugf("sys_task_info: time = %d, status = %d", kernel_ti.time, kernel_ti.status);
	return 0;

}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	curr_proc()->syscall_times[id]++;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	case SYS_sbrk:
		ret = sys_sbrk(args[0]);
		break;
	case SYS_mmap:
		ret = sys_mmap((void *)args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap((void *)args[0], args[1]);
		break;
	
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_task_info:
		ret = sys_task_info((TaskInfo *)args[0]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
