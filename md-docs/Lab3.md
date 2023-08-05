
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