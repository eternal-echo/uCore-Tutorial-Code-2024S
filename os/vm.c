#include "vm.h"
#include "defs.h"
#include "riscv.h"

pagetable_t kernel_pagetable;

extern char e_text[]; // kernel.ld sets this to end of kernel code.
extern char trampoline[];

// Make a direct-map page table for the kernel.
pagetable_t kvmmake()
{
	pagetable_t kpgtbl;
	kpgtbl = (pagetable_t)kalloc();
	memset(kpgtbl, 0, PGSIZE);
	// map kernel text executable and read-only.
	kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)e_text - KERNBASE,
	       PTE_R | PTE_X);
	// map kernel data and the physical RAM we'll make use of.
	kvmmap(kpgtbl, (uint64)e_text, (uint64)e_text, PHYSTOP - (uint64)e_text,
	       PTE_R | PTE_W);
	kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
	return kpgtbl;
}

// Initialize the one kernel_pagetable
// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvm_init()
{
	kernel_pagetable = kvmmake();
	w_satp(MAKE_SATP(kernel_pagetable));
	sfence_vma();
	infof("enable pageing at %p", r_satp());
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.

// 
// pagetable: 
// va: 
// alloc: 

/**
 * @brief [va->pa] 在页表中查找或创建一个PTE（页表项）
 * 			walk函数模拟了CPU进行MMU的过程。 SV39的转换是由3级页表结构完成。
 * 
 * @param pagetable **页表**的基地址
 * @param va 虚拟地址
 * @param alloc 如果需要的页表项不存在，是否分配新的页表页
 * @return pte_t* 
 * @ref 在riscv.h之中定义的宏函数PX完成了每一级从va转换到pte的过程:
 */
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc)
{
	// 检查虚拟地址是否超出最大允许范围
	if (va >= MAXVA)
		panic("walk");

	// 从最高级页表开始遍历（Sv39使用三级页表） MMU
	for (int level = 2; level > 0; level--) {
		// 获取当前级别的页表项指针
		pte_t *pte = &pagetable[PX(level, va)];
		if (*pte & PTE_V) {  // 如果页表项是有效的
			// 获取下一级页表的物理地址
			pagetable = (pagetable_t)PTE2PA(*pte);
		} else {  // 如果页表项无效
			// 如果不允许分配或内存分配失败，返回0
			if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
				return 0;
			// 初始化新分配的页表页
			memset(pagetable, 0, PGSIZE);
			// 设置页表项，标记为有效
			*pte = PA2PTE(pagetable) | PTE_V;
		}
	}
	// 返回最后一级页表中对应的页表项指针
	return &pagetable[PX(0, va)];
}

/**
 * @brief 查找虚拟地址对应的物理地址。Look up a virtual address, return the physical page, or 0 if not mapped.
 * 
 * @param pagetable 
 * @param va 虚拟地址。Can only be used to look up user pages.
 * @return uint64 return the physical page,如果地址未映射，返回0
 * @note 注意walkaddr函数没有考虑偏移量!
 */
uint64 walkaddr(pagetable_t pagetable, uint64 va)
{
	pte_t *pte;
	uint64 pa;

	// 检查虚拟地址是否超出最大允许范围
	if (va >= MAXVA)
		return 0;

	// 在页表中查找对应的页表项
	pte = walk(pagetable, va, 0);
	if (pte == 0)
		return 0;
	// 检查页表项是否有效
	if ((*pte & PTE_V) == 0)
		return 0;
	// 检查是否是用户页面
	if ((*pte & PTE_U) == 0)
		return 0;
	// 从页表项中提取物理地址
	pa = PTE2PA(*pte);
	return pa;
}


/**
 * @brief Look up a virtual address, return the physical address,
 * 
 * @param pagetable 
 * @param va 
 * @return uint64 
 * @note 考虑了偏移量，使用时优先考虑这个
 */
uint64 useraddr(pagetable_t pagetable, uint64 va)
{
	uint64 page = walkaddr(pagetable, va);
	if (page == 0)
		return 0;
	return page | (va & 0xFFFULL);
}

// Add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
	if (mappages(kpgtbl, va, sz, pa, perm) != 0)
		panic("kvmmap");
}



// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.

/**
 * @brief 建立新映射。mappages 在 pagetable 中建立 [va, va + size) 到 [pa, pa + size) 的映射，页表属性为perm
 * 
 * @param pagetable 
 * @param va 
 * @param size 
 * @param pa 
 * @param perm mappages的perm是用于控制页表项的flags的。请注意它具体指向哪几位，这将极大地影响页表的可用性。因为CPU进行MMU的时候一旦权限出错，比如CPU在U态访问了flag之中U=0的页表项是会直接报异常的。
 * @return int 0 on success, -1 if walk() couldn't allocate a needed page-table page.
 */
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
	uint64 a, last;
	pte_t *pte;

	a = PGROUNDDOWN(va);
	last = PGROUNDDOWN(va + size - 1);
	for (;;) {
		if ((pte = walk(pagetable, a, 1)) == 0) {
			errorf("pte invalid, va = %p", a);
			return -1;
		}
		if (*pte & PTE_V) {
			errorf("remap");
			return -1;
		}
		*pte = PA2PTE(pa) | perm | PTE_V;
		if (a == last)
			break;
		a += PGSIZE;
		pa += PGSIZE;
	}
	return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
/**
 * @brief 取消映射
 * 
 * @param pagetable 
 * @param va 
 * @param npages 
 * @param do_free do_free 控制是否 kfree 对应的物理内存（比如这是一个共享内存，那么第一次 unmap 就不 free，最后一个 unmap 肯定要 free）。
 */
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
	uint64 a;
	pte_t *pte;

	if ((va % PGSIZE) != 0)
		panic("uvmunmap: not aligned");

	for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
		if ((pte = walk(pagetable, a, 0)) == 0)
			continue;
		if ((*pte & PTE_V) != 0) {
			if (PTE_FLAGS(*pte) == PTE_V)
				panic("uvmunmap: not a leaf");
			if (do_free) {
				uint64 pa = PTE2PA(*pte);
				kfree((void *)pa);
			}
		}
		*pte = 0;
	}
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate(uint64 trapframe)
{
	pagetable_t pagetable;
	pagetable = (pagetable_t)kalloc();
	if (pagetable == 0) {
		errorf("uvmcreate: kalloc error");
		return 0;
	}
	memset(pagetable, 0, PGSIZE);
	if (mappages(pagetable, TRAMPOLINE, PAGE_SIZE, (uint64)trampoline,
		     PTE_R | PTE_X) < 0) {
		panic("mappages fail");
	}
	if (mappages(pagetable, TRAPFRAME, PGSIZE, trapframe, PTE_R | PTE_W) <
	    0) {
		panic("mappages fail");
	}
	return pagetable;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable)
{
	// there are 2^9 = 512 PTEs in a page table.
	for (int i = 0; i < 512; i++) {
		pte_t pte = pagetable[i];
		if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
			// this PTE points to a lower-level page table.
			uint64 child = PTE2PA(pte);
			freewalk((pagetable_t)child);
			pagetable[i] = 0;
		} else if (pte & PTE_V) {
			panic("freewalk: leaf");
		}
	}
	kfree((void *)pagetable);
}

/**
 * @brief Free user memory pages, then free page-table pages.
 *
 * @param max_page The max vaddr of user-space.
 */
void uvmfree(pagetable_t pagetable, uint64 max_page)
{
	if (max_page > 0)
		uvmunmap(pagetable, 0, max_page, 1);
	freewalk(pagetable);
}

/**
 * @brief 用于fork时复制父进程的页表和用户空间内存
 * 
 * @param old 父进程的页表
 * @param new 子进程的页表
 * @param max_page 需要复制的页表最大页数
 * @return int 成功返回0，失败返回-1
 * 
 * @details 该函数会遍历父进程的整个用户空间，为每个有效的页表项:
 * 1. 分配新的物理内存
 * 2. 复制内存内容
 * 3. 在子进程页表中建立相同的映射
 */
int uvmcopy(pagetable_t old, pagetable_t new, uint64 max_page)
{
	pte_t *pte;
	uint64 pa, i;
	uint flags;
	char *mem;

	// 遍历整个用户地址空间
	for(i = 0; i < max_page * PAGE_SIZE; i += PGSIZE){
		// 在父进程页表中查找页表项，不存在则继续
		if((pte = walk(old, i, 0)) == 0)
			continue;
		// 页表项无效则继续
		if((*pte & PTE_V) == 0)
			continue;
		
		// 获取物理地址和页表项标志位
		pa = PTE2PA(*pte);
		flags = PTE_FLAGS(*pte);
		
		// 为子进程分配新的物理页面
		if((mem = kalloc()) == 0)
			goto err;
		
		// 将父进程的页面内容复制到子进程的新页面
		memmove(mem, (char*)pa, PGSIZE);
		
		// 在子进程页表中建立映射，使用相同的权限标志
		if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
			kfree(mem);
			goto err;
		}
	}
	return 0;

err:
	// 发生错误时，清理已分配的内存和页表项
	uvmunmap(new, 0, i / PGSIZE, 1);
	return -1;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
	uint64 n, va0, pa0;

	while (len > 0) {
		va0 = PGROUNDDOWN(dstva);
		pa0 = walkaddr(pagetable, va0);
		if (pa0 == 0)
			return -1;
		n = PGSIZE - (dstva - va0);
		if (n > len)
			n = len;
		memmove((void *)(pa0 + (dstva - va0)), src, n);

		len -= n;
		src += n;
		dstva = va0 + PGSIZE;
	}
	return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
	uint64 n, va0, pa0;

	while (len > 0) {
		va0 = PGROUNDDOWN(srcva);
		pa0 = walkaddr(pagetable, va0);
		if (pa0 == 0)
			return -1;
		n = PGSIZE - (srcva - va0);
		if (n > len)
			n = len;
		memmove(dst, (void *)(pa0 + (srcva - va0)), n);

		len -= n;
		dst += n;
		srcva = va0 + PGSIZE;
	}
	return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
	uint64 n, va0, pa0;
	int got_null = 0, len = 0;

	while (got_null == 0 && max > 0) {
		va0 = PGROUNDDOWN(srcva);
		pa0 = walkaddr(pagetable, va0);
		if (pa0 == 0)
			return -1;
		n = PGSIZE - (srcva - va0);
		if (n > max)
			n = max;

		char *p = (char *)(pa0 + (srcva - va0));
		while (n > 0) {
			if (*p == '\0') {
				*dst = '\0';
				got_null = 1;
				break;
			} else {
				*dst = *p;
			}
			--n;
			--max;
			p++;
			dst++;
			len++;
		}

		srcva = va0 + PGSIZE;
	}
	return len;
}

/**
 * @brief 为进程分配虚拟内存空间。uvmalloc分配新的虚拟内存空间及对应的物理内存空间，建立页表映射关系。
 * 
 * @details 该函数负责为进程分配新的虚拟内存空间，将进程的虚拟内存从oldsz增长到newsz。
 * 函数会**分配物理内存页面**并**建立相应的页表映射关系**。如果分配失败，会释放已分配的资源。
 * 
 * 具体步骤:
 * 1. 检查新大小是否小于原大小
 * 2. 将旧大小向上对齐到页面大小
 * 3. 循环分配物理页面并建立映射:
 *    - 调用 kalloc() 分配物理内存
 *    - 清零新分配的内存页面
 *    - 通过 mappages() 建立虚拟地址到物理地址的映射
 * 
 * @param pagetable 进程的页表
 * @param oldsz 原始**虚拟地址**
 * @param newsz 新的**虚拟地址**
 * @param xperm 额外的页表权限标志：PTE_V=(1L << 0)=1，PTE_R=(1L << 1)=2，PTE_W=(1L << 2)=4，PTE_X=(1L << 3)=8，PTE_U=(1L << 4)=16
 * @return uint64 成功返回新虚拟地址
 * 
 * @note 
 * - newsz 无需按页对齐
 * - 页表项默认包含 PTE_R|PTE_U 权限，可通过xperm参数添加额外权限
 * - 失败时会调用 uvmdealloc 清理已分配的资源
 */
// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
	char *mem;
	uint64 a;

	// 如果新大小小于旧大小,直接返回旧大小
	if(newsz < oldsz)
		return oldsz;

	// 将旧大小向上对齐到页面大小
	oldsz = PGROUNDUP(oldsz);
	
	// 从旧大小开始,每次增加一个页面大小,直到达到新大小
	for(a = oldsz; a < newsz; a += PGSIZE){
		// 分配一个物理页面
		mem = kalloc();
		if(mem == 0){
			// 如果分配失败,释放已分配的内存并返回0
			uvmdealloc(pagetable, a, oldsz);
			return 0;
		}
		// 将新分配的物理页面清零
		memset(mem, 0, PGSIZE);
		
		// 建立虚拟地址到物理地址的映射
		// 注：如果指定的虚拟地址范围已经被映射过，会在 mappages() 函数中失败返回-1
		// PTE_R|PTE_U 表示用户可读,xperm为额外权限
		if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
			// 如果映射失败,释放物理内存并清理已分配的页表项
			kfree(mem);
			uvmdealloc(pagetable, a, oldsz); 
			return 0;
		}
	}
	return newsz;
}

/**
 * @brief 释放用户进程的内存空间，将虚拟内存从oldsz调整为newsz
 *
 * @param pagetable 需要调整的进程的页表
 * @param oldsz 原始虚拟地址
 * @param newsz 新的虚拟地址
 * @return uint64 调整后的实际大小
 * 
 * @note oldsz和newsz不需要按页对齐，newsz也不必小于oldsz
 *       oldsz可以大于实际进程大小
 * 
 * @details 函数处理步骤：
 * 1. 如果新大小大于等于原大小，直接返回原大小
 * 2. 计算需要释放的页数：
 *    - 将oldsz和newsz向上对齐到页边界
 *    - 计算这两个边界之间的页数
 * 3. 调用uvmunmap释放这些页，最后一个参数1表示要释放物理内存
 */
// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
	if(newsz >= oldsz)
			return oldsz;

	
	if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
			int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
			uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
	}

	return newsz;
}

