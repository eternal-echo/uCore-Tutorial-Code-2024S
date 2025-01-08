#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "proc.h"

uint64 sys_write(int fd, char *str, uint len)
{
	debugf("sys_write fd = %d str = %x, len = %d", fd, str, len);
	if (fd != STDOUT)
		return -1;
	for (int i = 0; i < len; ++i) {
		console_putchar(str[i]);
	}
	return len;
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

uint64 sys_gettimeofday(TimeVal *val, int _tz)
{
	uint64 cycle = get_cycle();
	val->sec = cycle / CPU_FREQ;
	val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
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
	struct proc *p = curr_proc();
	if (p == 0) {
		return 1;
	}

	ti->status = Running;

	for (int i = 0; i < MAX_SYSCALL_NUM; i++) {
		ti->syscall_times[i] = p->syscall_times[i];
	}

	int current_time = (int)((get_cycle() % CPU_FREQ) * 1000 / CPU_FREQ);
	ti->time = current_time - p->time;

	tracef("sys_task_info: current_time = %d, proc's time = %d", current_time, p->time);

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
		ret = sys_write(args[0], (char *)args[1], args[2]);
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
