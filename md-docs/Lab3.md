
# Lab3

> [实验文档](https://pdos.csail.mit.edu/6.828/2022/labs/pgtbl.html)

```bash
git fetch
git checkout pgtbl
make clean
```

## Speed up system calls

第一个实验的内容是在进程创建之时手动添加一个只读页面用于加速一些系统调用

开始之前首先需要修改一些 clangd 的配置, 开启 LAB_PGTBL 的宏支持, 以获得更好的编码体验

```json
{
    "clangd.fallbackFlags": [
        "-I${workspaceFolder}",
        "-DLAB_PGTBL"
    ]
}
```

ugetpid 的实现位于 user/ulib.c, 可以注意到只是对于 USYSCALL 地址取 struct usyscall 结构体然后读取 pid 返回, 考虑到进程的独立性, 我们需要为每一个进程的 USYSCALL 地址单独添加一个只读页面. usyscall 结构体的定义(kernel/memlayout.h)也相当简单, 只有一个 pid

```c
#ifdef LAB_PGTBL
int
ugetpid(void)
{
  struct usyscall *u = (struct usyscall *)USYSCALL;
  return u->pid;
}
#endif
```

首先我们修改 kernel/proc.h 中 proc 结构体成员, 添加一个 `struct usyscall *usyscallpage`

```c
struct proc {
  // ...
  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct usyscall *usyscallpage; // <---- here
  struct context context;      // swtch() here to run process
  // ...
};
```

在 allocproc 中为 usyscallpage 调用 kalloc 初始化分配一个页面, 然后将 usyscallpage 的 pid 设置为当前进程的 pid

```c
static struct proc *allocproc(void) {
    // ...
found:
    p->pid = allocpid();
    p->state = USED;

    // Allocate a trapframe page.
    if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    if ((p->usyscallpage = (struct usyscall *)kalloc()) == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    p->usyscallpage->pid = p->pid;
    // ...
    return p;
}
```

接下来修改 proc_pagetable, 仿照 trapframe 的方式调用 mappages 进行页面映射, mappages 的函数原型定义如下

```c
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm);
```


将其映射到 USYSCALL 的位置, 设置标志位为 `PTE_R | PTE_U`, 只读 + 用户可访问.

```c
// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t proc_pagetable(struct proc *p) {
    pagetable_t pagetable;

    // An empty page table.
    pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) < 0) {
        uvmfree(pagetable, 0);
        return 0;
    }

    // map the trapframe page just below the trampoline page, for
    // trampoline.S.
    if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe), PTE_R | PTE_W) < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    if (mappages(pagetable, USYSCALL, PGSIZE, (uint64)p->usyscallpage, PTE_R | PTE_U) < 0) {
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}
```

至此分配过程中的页面映射已经完成, 接下来需要处理释放过程. 修改 freeproc 仿照 trapfram 进行页面的释放

```c
static void freeproc(struct proc *p) {
    if (p->trapframe)
        kfree((void *)p->trapframe);
    p->trapframe = 0;
    if (p->usyscallpage) {
        kfree((void *)p->usyscallpage);
    }
    p->usyscallpage = 0;
    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}
```

同时注意修改 proc_freepagetable, 完成对于 USYSCALL 的页面去掉映射

```c
// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmunmap(pagetable, USYSCALL, 1, 0);
    uvmfree(pagetable, sz);
}
```

这个可以测试通过

```bash
make GRADEFLAGS=ugetpid grade

== Test pgtbltest == (2.1s)
== Test   pgtbltest: ugetpid ==
  pgtbltest: ugetpid: OK
```

内部执行 pgtbltest 时 ugetpid_test 成功

```bash
$ pgtbltest
ugetpid_test starting
ugetpid_test: OK
pgaccess_test starting
pgtbltest: pgaccess_test failed: incorrect access bits set, pid=3
```

**Q: Which other xv6 system call(s) could be made faster using this shared page? Explain how.**

这里的 using this shared page 并不是单纯指 usyscallpage 这个结构, 而是指这种添加多分配一个只读页面的加速方式. 所以对于任何直接或间接调用 copyout 功能的系统调用都将被加速, 因为它节省了复制数据的时间.此外,纯粹用于信息检索的系统调用(如本节中的 getpid, fstat, lstat 等)也会更快.这是因为不再需要因为间接调用 sys 系统调用而导致陷入和退出内核模式的耗时, 而是可以在用户模式下读取相应的数据.

## Print a page table

第二题要求对于一个页表 pagetable 按照格式输出其所有页表项

首先按照说明在 kernel/defs.h 添加函数定义, kernel/exec.c 中添加函数调用

然后浏览一下 freewalk 函数, 已经给出了比较关键的函数使用方法, 即 `pte_t pte = pagetable[i];` 获取 PTE, `uint64 child = PTE2PA(pte);` 计算对应的 PA, 并递归调用直至最后一级页表

```c
// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable) {
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
```

因此可以仿照完成 `vmprint`, 递归调用 vm_table_print 输出信息, depth 为深度信息. 对于最后一级页表 `else if (pte & PTE_V)` 的情况不再做递归处理

```c
void vm_table_print(pagetable_t pagetable, int depth) {
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // this PTE points to a lower-level page table.
            uint64 child = PTE2PA(pte);
            printf("..");
            for (int i = 0; i < depth; i++) {
                printf(" ..");
            }
            printf("%d: pte %p pa %p\n", i, pte, (pagetable_t)child);
            vm_table_print((pagetable_t)child, depth + 1);
        } else if (pte & PTE_V) {
            uint64 child = PTE2PA(pte);
            printf("..");
            for (int i = 0; i < depth; i++) {
                printf(" ..");
            }
            printf("%d: pte %p pa %p\n", i, pte, (pagetable_t)child);
        }
    }
}

void vmprint(pagetable_t pagetable) {
    printf("page table %p\n", pagetable);
    vm_table_print(pagetable, 0);
}
```

> 注意这里的输出格式即可

```bash
page table 0x0000000087f6b000
..0: pte 0x0000000021fd9c01 pa 0x0000000087f67000
.. ..0: pte 0x0000000021fd9801 pa 0x0000000087f66000
.. .. ..0: pte 0x0000000021fda01b pa 0x0000000087f68000
.. .. ..1: pte 0x0000000021fd9417 pa 0x0000000087f65000
.. .. ..2: pte 0x0000000021fd9007 pa 0x0000000087f64000
.. .. ..3: pte 0x0000000021fd8c17 pa 0x0000000087f63000
..255: pte 0x0000000021fda801 pa 0x0000000087f6a000
.. ..511: pte 0x0000000021fda401 pa 0x0000000087f69000
.. .. ..509: pte 0x0000000021fdcc13 pa 0x0000000087f73000
.. .. ..510: pte 0x0000000021fdd007 pa 0x0000000087f74000
.. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
```

```bash
make GRADEFLAGS=printout grade
```

**Explain the output of vmprint in terms of Fig 3-4 from the text. What does page 0 contain? What is in page 2? When running in user mode, could the process read/write the memory mapped by page 1? What does the third to last page contain?**

> 这个问题我看了好多大佬写的博客, 感觉说的答案的都不太对...

vmprint 函数输出了 7 个页表项, 那么我们需要做的是了解清楚这 7 个页表对应的是什么内容, 在哪里/何时被创建的.

首先浏览一下 exec 函数, 关键部分截取如下所示. 先通过 `pagetable = proc_pagetable(p)` 创建一个初始进程页表. 然后读取 user/_init 的 ELF 文件并进行解析. 其中影响页表的操作是 `uvmalloc`, 可以发现有两处使用了 uvmalloc, 分别是在 ELF 文件解析时以及后面出现一次, 我们分别来分析一下

> 更准确是说是 uvmalloc 内部的 `mappages` 完成了对页表项的添加

```c
int exec(char *path, char **argv) {
    // ...
    if ((pagetable = proc_pagetable(p)) == 0)
            goto bad;

    // Load program into memory.
    for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
        if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
            goto bad;
        if (ph.type != ELF_PROG_LOAD) {
            continue;
        }
        if (ph.memsz < ph.filesz)
            goto bad;
        if (ph.vaddr + ph.memsz < ph.vaddr)
            goto bad;
        if (ph.vaddr % PGSIZE != 0)
            goto bad;
        uint64 sz1;
        if ((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)
            goto bad;
        sz = sz1;
        if (loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
            goto bad;
    }
    iunlockput(ip);
    end_op();
    ip = 0;

    p = myproc();
    uint64 oldsz = p->sz;

    // Allocate two pages at the next page boundary.
    // Make the first inaccessible as a stack guard.
    // Use the second as the user stack.
    sz = PGROUNDUP(sz);
    uint64 sz1;
    if ((sz1 = uvmalloc(pagetable, sz, sz + 2 * PGSIZE, PTE_W)) == 0)
        goto bad;
    sz = sz1;
    uvmclear(pagetable, sz - 2 * PGSIZE);
    // ...
    if (p->pid == 1) {
        vmprint(p->pagetable);
    }
    // ...
}
```

我们首先在 `pagetable = proc_pagetable(p)` 后面添加一个 vmprint(pagetable) 查看一下初始化之时的页表情况, 如下所示. 不难发现 page509 510 511 三个页表分别对应 TRAMPOLINE TRAPFRAME USYSCALL

```bash
page table 0x0000000087f6b000
..255: pte 0x0000000021fda801 pa 0x0000000087f6a000
.. ..511: pte 0x0000000021fda401 pa 0x0000000087f69000
.. .. ..509: pte 0x0000000021fdcc13 pa 0x0000000087f73000
.. .. ..510: pte 0x0000000021fdd007 pa 0x0000000087f74000
.. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
```

接下来看一下 ELF 内部的 uvmalloc, 首先可以使用 objdump 工具来查看一下 user/_init (即第一个进程 pid = 1) 的段表, 如下所示. 可以看到有四个段, 同时注意到内部逻辑中使用了 `(ph.type != ELF_PROG_LOAD)` 进行判断, 所以只会读取并为 LOAD 段分配内存. 两个 LOAD 段大小分别为 4096 和 16, 对应 TEXT 与 DATA 段. 所以 page0 page1 分别对应 TEXT 和 DATA 段, 用户态也可以 读/写 page1

```bash
(base) kamilu@LZX:~/xv6-labs-2022$ objdump -p user/_init

user/_init:     file format elf64-little

Program Header:
0x70000003 off    0x0000000000006ce4 vaddr 0x0000000000000000 paddr 0x0000000000000000 align 2**0
         filesz 0x0000000000000033 memsz 0x0000000000000000 flags r--
    LOAD off    0x0000000000001000 vaddr 0x0000000000000000 paddr 0x0000000000000000 align 2**12
         filesz 0x0000000000001000 memsz 0x0000000000001000 flags r-x
    LOAD off    0x0000000000002000 vaddr 0x0000000000001000 paddr 0x0000000000001000 align 2**12
         filesz 0x0000000000000010 memsz 0x0000000000000030 flags rw-
   STACK off    0x0000000000000000 vaddr 0x0000000000000000 paddr 0x0000000000000000 align 2**4
         filesz 0x0000000000000000 memsz 0x0000000000000000 flags rw-
```

接下来作者源代码写了一段注释给出了说明: 即 page2 为堆栈保护, page3 是用户栈

```c
// Allocate two pages at the next page boundary.
// Make the first inaccessible as a stack guard.
// Use the second as the user stack.
```

书中图 3.4 如下, 可以在上面看到 vmprint 共输出了 7 条页表信息, 刚好对应下图中 7 个部分(usyscallpage 是上题中添加的为了加速的页面)

![20230807172130](https://raw.githubusercontent.com/learner-lu/picbed/master/20230807172130.png)

## Detect which pages have been accessed

第三个实验的内容是完成一个系统调用 pgaccess 用于检测页面是是否被访问了, 先浏览 user/pgtbltest.c 看一下 pgaccess 的使用方法, 对应的代码如下. 可以看到大概意思是说访问了 1 2 30 三个数组处的值, 此时 abits 中对应这三位的 bit map 应为 1.

```c
void pgaccess_test() {
    char *buf;
    unsigned int abits;
    printf("pgaccess_test starting\n");
    testname = "pgaccess_test";
    buf = malloc(32 * PGSIZE);
    if (pgaccess(buf, 32, &abits) < 0)
        err("pgaccess failed");
    buf[PGSIZE * 1] += 1;
    buf[PGSIZE * 2] += 1;
    buf[PGSIZE * 30] += 1;
    if (pgaccess(buf, 32, &abits) < 0)
        err("pgaccess failed");
    if (abits != ((1 << 1) | (1 << 2) | (1 << 30)))
        err("incorrect access bits set");
    free(buf);
    printf("pgaccess_test: OK\n");
}
```

实验比较贴心的帮我们把创建一个新的系统调用的重复性工作完成了, 只需要完成 kernel/sysproc.c 中的 sys_pgaccess 即可

首先阅读 RISCV 手册找到 xv6 对应的 sv39, 其 PTE 的标志位第 6 位对应 Access

> P85

![20230807212422](https://raw.githubusercontent.com/learner-lu/picbed/master/20230807212422.png)

修改 kernel/riscv.h 添加 PTE_A 标志位

```c
#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // user can access
#define PTE_A (1L << 6)
```

关于标志位 A 的描述信息如下, 文中特别提及了需要原子操作, 所以需要注意上锁

> P81

![20230807212343](https://raw.githubusercontent.com/learner-lu/picbed/master/20230807212343.png)

代码实现如下, 先获取三个参数, 然后为当前进程上锁, 使用 `addr + i*PGSIZE` 以虚拟页大小为单位遍历每一个虚拟地址, 调用 walk 通过 va 得到页表地址, 判断其 `PTE_A` 位是否为 1, 如果为 1 则添加 mask 对应位置并置零, 最后解锁, 然后使用 copyout 将 mask 复制到 mask_addr 的位置

```c
#ifdef LAB_PGTBL
int sys_pgaccess(void) {
    
    uint64 addr, mask_addr;
    int len;
    argaddr(0, &addr);
    argint(1, &len);
    argaddr(2, &mask_addr);

    struct proc *p = myproc();
    uint64 mask = 0;
    acquire(&p->lock);
    for (int i=0;i<len;i++) {
        pte_t* pte = walk(p->pagetable, addr + i*PGSIZE, 0);
        if (*pte & PTE_A) {
            mask |= (1 << i);
            *pte ^= PTE_A;
        }
    }
    release(&p->lock);
    if(copyout(p->pagetable, mask_addr, (char *)&mask, sizeof(mask)) < 0)
      return -1;
    return 0;
}
#endif
```

```bash
(base) kamilu@LZX:~/xv6-labs-2022$ make grade
== Test pgtbltest ==
$ make qemu-gdb
(4.0s)
== Test   pgtbltest: ugetpid ==
  pgtbltest: ugetpid: OK
== Test   pgtbltest: pgaccess ==
  pgtbltest: pgaccess: OK
== Test pte printout ==
$ make qemu-gdb
pte printout: OK (0.5s)
== Test answers-pgtbl.txt == answers-pgtbl.txt: FAIL
    Cannot read answers-pgtbl.txt
== Test usertests ==
$ make qemu-gdb
(71.4s)
== Test   usertests: all tests ==
  usertests: all tests: OK
== Test time ==
time: FAIL
    Cannot read time.txt
Score: 40/46
```

> fail 的两个是 answer 和 time.txt, 无所谓

## 参考

- [6.s081_2022_lab3](https://jinzhec2.github.io/blog/post/6.s081_2022_lab3/)
- [xv6-labs-2022-solutions](https://github.com/relaxcn/xv6-labs-2022-solutions)