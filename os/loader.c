#include "loader.h"
#include "defs.h"
#include "trap.h"

/**
 * @brief 用户应用程序管理的全局变量
 * 
 * @var app_num         应用程序总数
 * @var app_info_ptr    指向存储应用程序信息的数组（包含程序的起始/结束地址）
 * @var _app_num        外部符号：存储应用程序数量（由链接器设置）
 * @var _app_names      外部符号：存储应用程序名称（由链接器设置）
 * @var INIT_PROC       外部符号：指向初始进程
 * @var names           存储应用程序名称的数组（最多MAX_APP_NUM个应用，每个名称最长MAX_STR_LEN字符）
 * 
 * 这些变量用于管理嵌入在内核中的用户应用程序。
 * 变量值在内核初始化期间从链接脚本提供的信息中填充。
 */
static int app_num;
static uint64 *app_info_ptr;
extern char _app_num[], _app_names[], INIT_PROC[];
char names[MAX_APP_NUM][MAX_STR_LEN];

/**
 * @brief 初始化加载器，读取并设置用户程序信息
 * 
 * 该函数通过link_app.S中预定义的符号获取用户程序信息，包括：
 * - 程序数量
 * - 程序名称列表
 * - 程序在内存中的位置信息
 */
void loader_init()
{
	char *s;
	// _app_num指向程序信息区域的起始位置，第一个uint64存储程序数量
	app_info_ptr = (uint64 *)_app_num;
	app_num = *app_info_ptr;
	// 指针移动到程序地址信息区域
	app_info_ptr++;
	
	// _app_names指向存储所有程序名称的字符串区域
	s = _app_names;
	printf("app list:\n");
	
	// 遍历并保存所有程序的名称
	for (int i = 0; i < app_num; ++i) {
		// 获取当前程序名称的长度
		int len = strlen(s);
		// 将程序名称复制到全局names数组中
		strncpy(names[i], (const char *)s, len);
		// 移动到下一个程序名称的起始位置（跳过当前名称和结尾的'\0'）
		s += len + 1;
		// 打印程序名称用于调试
		printf("%s\n", names[i]);
	}
}
/**
 * @brief 根据应用程序名称查找其ID
 * @param name 要查找的应用程序名称
 * @return 成功返回应用程序ID，失败返回-1
 */
int get_id_by_name(char *name)
{
	// 遍历应用程序名称数组查找匹配
	for (int i = 0; i < app_num; ++i) {
		if (strncmp(name, names[i], 100) == 0)
			return i;
	}
	warnf("Cannot find such app %s", name);
	return -1;
}

/**
 * @brief 二进制加载器，负责将应用程序加载到进程的地址空间
 * @param start 应用程序二进制在内核中的起始地址
 * @param end 应用程序二进制在内核中的结束地址
 * @param p 目标进程结构体
 * @return 成功返回0，失败触发panic
 */
int bin_loader(uint64 start, uint64 end, struct proc *p)
{
	if (p == NULL || p->state == UNUSED)
		panic("...");

	void *page;
	// 注意现在我们不要求对齐了，代码的核心逻辑还是把 [start, end)
	// 映射到虚拟内存的 [BASE_ADDRESS, BASE_ADDRESS + length)

	// 计算需要映射的物理地址范围（页对齐）
	uint64 pa_start = PGROUNDDOWN(start);
	uint64 pa_end = PGROUNDUP(end);
	uint64 length = pa_end - pa_start;
	// 设置用户空间的虚拟地址映射范围
	uint64 va_start = BASE_ADDRESS;
	uint64 va_end = BASE_ADDRESS + length;

	// 对于 .bin 的每一页，都申请一个新页并进行内容拷贝，最后建立这一页的映射。
	// 为什么要拷贝呢？Lab4直接把源程序镜像所在的位置映射过来了，不能第二次执行
	// 因为 .data 和 .bss 段数据都被上一次执行改掉了，不是初始化的状态。每个程序仅能运行一次。

	// 为程序代码和数据分配物理页并建立映射
	// 不再一次 map 很多页面，而是逐页 map，为什么？
	for (uint64 va = va_start, pa = pa_start; pa < pa_end;
		 va += PGSIZE, pa += PGSIZE) {
		// 这里我们不会直接映射，而是新分配一个页面，然后使用 memmove 进行拷贝
		// 这样就不会有对其的问题了，但为何这么做其实有更深层的原因。
		page = kalloc();
		if (page == 0) {
			panic("...");
		}
		// 复制程序内容到新分配的物理页
		memmove(page, (const void *)pa, PGSIZE);
		// 处理页边界的特殊情况，确保未使用的部分被清零
		// 这个 if 就是为了防止 start end 不对其导致拷贝了多余的内核数据，我们需要手动把它们清空
		if (pa < start) {
			memset(page, 0, start - va);
		} else if (pa + PAGE_SIZE > end) {
			memset(page + (end - pa), 0, PAGE_SIZE - (end - pa));
		}
		// 建立用户态的页表映射，设置权限为用户可读写执行
		if (mappages(p->pagetable, va, PGSIZE, (uint64)page,
					 PTE_U | PTE_R | PTE_W | PTE_X) != 0)
			panic("...");
	}

	// 分配并映射用户栈空间
	p->ustack = va_end + PAGE_SIZE;
	for (uint64 va = p->ustack; va < p->ustack + USTACK_SIZE;
		 va += PGSIZE) {
		page = kalloc();
		if (page == 0) {
			panic("...");
		}
		memset(page, 0, PGSIZE);
		// 用户栈的页表项权限设置为可读写（不可执行）
		if (mappages(p->pagetable, va, PGSIZE, (uint64)page,
					 PTE_U | PTE_R | PTE_W) != 0)
			panic("...");
	}

	// 设置进程的关键参数
	p->trapframe->sp = p->ustack + USTACK_SIZE;    // 栈指针指向栈顶
	p->trapframe->epc = va_start;                  // 入口点设置为程序起始地址
	p->max_page = PGROUNDUP(p->ustack + USTACK_SIZE - 1) / PAGE_SIZE;  // 计算最大页数
	p->program_brk = p->ustack + USTACK_SIZE;      // 初始化程序break位置
	p->heap_bottom = p->ustack + USTACK_SIZE;      // 初始化堆底位置
	p->state = RUNNABLE;                           // 将进程状态设置为可运行
	return 0;
}

/**
 * @brief 加载指定ID的应用程序到进程空间
 * @param app_id 要加载的应用程序ID
 * @param p 目标进程结构体
 * @return 通过调用bin_loader完成加载
 */
int loader(int app_id, struct proc *p)
{
	// app_info_ptr数组中相邻两个元素分别是程序的起始和结束地址
	return bin_loader(app_info_ptr[app_id], app_info_ptr[app_id + 1], p);
}

// load all apps and init the corresponding `proc` structure.
int load_init_app()
{
	int id = get_id_by_name(INIT_PROC);
	if (id < 0)
		panic("Cannpt find INIT_PROC %s", INIT_PROC);
	struct proc *p = allocproc();
	if (p == NULL) {
		panic("allocproc\n");
	}
	debugf("load init proc %s", INIT_PROC);
	loader(id, p);
	add_task(p);
	return 0;
}
