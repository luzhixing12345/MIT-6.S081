
# Lab5

> [实验文档](https://pdos.csail.mit.edu/6.828/2022/labs/cow.html)

lab5 只有这一个实验, 实现写时复制, 本章没有对应任何一个章节, 算是对于前面页表和中断知识点的总结复习

## Implement copy-on-write fork

在 kernel/defs.h 添加两个函数声明, 分别用于判断当前页面是否是一个写时复制的页面, 以及去分配一个写时分配的页面

```c
int             is_cow_page(pagetable_t pagetable, uint64 va);
uint64          alloc_cow_page(pagetable_t pagetable, uint64 va);
```

在 kernel/kalloc.c 中声明两个全局变量, 分别用于记录所有物理页的引用数量, 以及一个锁

```c
int page_reference[PHYSTOP / PGSIZE];
struct spinlock reference_count_lock;
```

修正 kfree 和 kalloc, 对于需要释放的页面 pa, 将其对应的 page_reference 引用数减一, 如果剩余引用数 <= 0, 则释放页面并加入到空闲链表中. 这里需要注意两个地方, 一个是锁reference_count_lock的释放时机, 在完成对于 page_reference 的操作即释放, 不要嵌套 acquire; 第二个是引用判断条件是 `<= 0` , 因为在系统运行开始的时候,需要对空闲页列表 (kmem.freelist) 进行初始化,此时的引用计数就为 -1 

对于 kalloc, 初始化 page_reference 设置为 1 即可

```c
void kfree(void *pa) {
    struct run *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    acquire(&reference_count_lock);
    page_reference[(uint64)pa / PGSIZE] -= 1;
    if (page_reference[(uint64)pa / PGSIZE] <= 0) {
        release(&reference_count_lock);
        // Fill with junk to catch dangling refs.
        memset(pa, 1, PGSIZE);

        r = (struct run *)pa;

        acquire(&kmem.lock);
        r->next = kmem.freelist;
        kmem.freelist = r;
        release(&kmem.lock);
    } else {
        release(&reference_count_lock);
    }
}

void *kalloc(void) {
    struct run *r;

    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r) {
        kmem.freelist = r->next;
        acquire(&reference_count_lock);
        page_reference[(uint64)r / PGSIZE] = 1;
        release(&reference_count_lock);
    }
    release(&kmem.lock);

    if (r)
        memset((char *)r, 5, PGSIZE);  // fill with junk
    return (void *)r;
}
```

接下来看一下标志位, sv39 和 sv32 的标志位含义相同, 如下图所示

![20230809092832](https://raw.githubusercontent.com/learner-lu/picbed/master/20230809092832.png)

- V 位表示 PTE 是否有效. 如果 V 为 0,则 PTE 中的所有其他位都是无关位,软件可以自由使用.
- 权限位 R、W 和 X 分别表示页面是否可读、可写和可执行. **当这三个位都为零时,PTE 是指向页表下一级的指针;否则,它就是叶 PTE.**
- **可写页面也必须标记为可读;相反的组合则保留留作将来使用**. 下图概述了 X/W/R 的组合情况

  其中 `000` 代表 1级/2级页表, `010` 和 `110` 中 W/R 并没有统一, 留作后期使用

  ![20230809092813](https://raw.githubusercontent.com/learner-lu/picbed/master/20230809092813.png)

- U 位表示页面是否可由用户模式访问.U 模式软件只有在 U=1 时才能访问页面.
- G 位表示全局映射.全局映射是指存在于所有地址空间中的映射.对于非叶 PTE,全局设置意味着页表后续级别中的所有映射都是全局映射.表中的所有映射都是全局映射.

  > 暂未使用

- RSW 字段保留给监控软件使用;执行时应忽略该字段
- 每个叶 PTE 包含一个已访问 (A) 位和一个脏 (D) 位.A 位表示虚拟页面在上次清除 A 位后被读取、写入或获取.D 位表示自上次清除 D 位后,虚拟页面已被写入.

  > 在 Lab3 Detect which pages have been accessed 实验中被使用过

- 试图从没有执行权限的页面获取指令时,会引发获取页面故障异常.
- 试图执行有效地址位于无读取权限页面内的加载或加载保留指令,会引发加载页面故障异常.
- 试图执行有效地址位于无写入权限页面内的存储、存储条件或 AMO 指令,会引发存储页面故障异常.
- AMO 不会引发加载页面故障异常.因为任何不可读页面也是不可写页面、
- 试图在不可读页面上执行 AMO 时,总是会引发存储页面故障异常.

所以我们可以使用 RSW 字段, 这里我们使用第八位, 若为 1 则说明为 COW 页面. 在 kernel/riscv.h 中定义宏 PTE_COW

```c
#define PTE_COW (1L << 8) // copy on write
```

[riscv-privileged-20211203.pdf](https://github.com/riscv/riscv-isa-manual/releases/download/Priv-v1.12/riscv-privileged-20211203.pdf) 4.1.9 中标注了 scause 值为 15 时对应 AMO 页错误的情况

![20230809163412](https://raw.githubusercontent.com/learner-lu/picbed/master/20230809163412.png)

在 kernel/trap.c 中添加一个条件判断, 对于 15 号中断信号获取其 va 地址, 然后做一些判断, 调用 alloc_cow_page 分配页面, 如果分配失败则设置 killed 标志位为 1

```c
void usertrap(void) {
    // ...
    int scause = r_scause();
    if (scause == 8) {
        // system call
    } else if (scause == 15) {
        uint64 va = r_stval();
        if (va >= p->sz) {
            p->killed = 1;
        }

        if (alloc_cow_page(p->pagetable, va) == 0) {
            p->killed = 1;
        }

    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else {
        printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        setkilled(p);
    }
    // ...
}
```

接下来的所有函数修改均在 kernel/vm.c, 引入前面定义的东西

```c
#include "spinlock.h"
#include "proc.h"

extern int page_reference[PHYSTOP / PGSIZE];
extern struct spinlock reference_count_lock;
```

首先对于具有写权限 `PTE_W` 的页面取消其写权限, 并添加 `PTE_COW` 标记为写时复制页面, 加锁并更新页面的引用数量. 最后将新页面的页表直接映射为旧页面的 pa. 此时 fork 的两个父子进程具有相同的页表信息

```c
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
    pte_t *pte;
    uint64 pa, i;
    uint flags;

    for (i = 0; i < sz; i += PGSIZE) {
        if ((pte = walk(old, i, 0)) == 0)
            panic("uvmcopy: pte should exist");
        if ((*pte & PTE_V) == 0)
            panic("uvmcopy: page not present");
        pa = PTE2PA(*pte);

        if (*pte & PTE_W) {
            *pte ^= PTE_W;
            *pte |= PTE_COW;
        }
        acquire(&reference_count_lock);
        page_reference[(uint64)pa / PGSIZE] += 1;
        release(&reference_count_lock);
        flags = PTE_FLAGS(*pte);

        if (mappages(new, i, PGSIZE, (uint64)pa, flags) != 0) {
            goto err;
        }
    }
    return 0;
err:
    uvmunmap(new, 0, i / PGSIZE, 1);
    return -1;
}
```

判断页面是否是 cow 只需要判断 PTE_V 和 PTE_COW 两个标志位. 分配页面 alloc_cow_page 要麻烦一些, 首先拿到 pte 和 pa, 接下来判断如果当前 pa 的页面引用为 1, 则说明没有其他进程含有对该页面的引用了, 则当前进程可以直接使用这个页面, 修改 pte 的标志位, 设置 PTE_W 以及取消 PTE_COW

如果还有其他页面具有对该页面的引用, 则使用 kalloc 分配一个新页面, 将页面复制过去之后释放掉当前页面(这里的释放不是指真的释放, 而是减一个对于页面的引用), 通过 `PA2PTE(mem)` 使用新页面的 pte 修改当前页面的 pte 值, 添加 PTE_W 去掉 PTE_COW, 返回新分配的页地址 mem

> 这里需要注意的是 if (va >= MAXVA) 这个判断条件要有, 不然 unittest 的时候会因为 va 的参数导致 panic walk 失败

```c
int is_cow_page(pagetable_t pagetable, uint64 va) {
    pte_t *pte = walk(pagetable, va, 0);
    return (*pte & PTE_V) && (*pte & PTE_COW);
}

uint64 alloc_cow_page(pagetable_t pagetable, uint64 va) {
    char *mem;
    if (va >= MAXVA)
        return 0;
    pte_t *pte = walk(pagetable, va, 0);
    uint64 pa = walkaddr(pagetable, va);
    if (pte == 0) {
        return -1;
    }
    if ((*pte & PTE_COW) == 0 || (*pte & PTE_U) == 0 || (*pte & PTE_V) == 0) {
        return -1;
    }

    if (page_reference[pa / PGSIZE] == 1) {
        *pte |= PTE_W;
        *pte ^= PTE_COW;
        return pa;
    } else {
        if ((mem = kalloc()) == 0) {
            return -1;
        }
        memmove(mem, (char *)pa, PGSIZE);
        kfree((void *)pa);
        uint flags = PTE_FLAGS(*pte);
        *pte = (PA2PTE(mem) | flags | PTE_W);
        *pte ^= PTE_COW;
        return (uint64)mem;
    }
}
```

最后对于 copyout, 当从内核空间复制到用户空间, 用户空间的地址 dstva 所在的 va0 也可能是一个写时复制的共享页面, 因此需要判断一下对于有效的页面, 如果是一个 COW 页面, 则调用 alloc_cow_page 分配一个新页面替换掉之前的 pa0

```c
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
    uint64 n, va0, pa0;

    while (len > 0) {
        va0 = PGROUNDDOWN(dstva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        struct proc *p = myproc();
        pte_t *pte = walk(pagetable, va0, 0);
        if (*pte == 0)
            p->killed = 1;
        if (is_cow_page(pagetable, va0)) {
            pa0 = alloc_cow_page(pagetable, va0);
        }
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
```

```bash
$ cowtest
simple: ok
simple: ok
three: ok
three: ok
three: ok
file: ok
$ usertests -q
...
ALL TESTS PASSED
```