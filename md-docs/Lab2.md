
# Lab2

> 实验文档: [lab2 syscall](https://pdos.csail.mit.edu/6.828/2022/labs/syscall.html)

```bash
git fetch
git checkout syscall
```

> 切换到 syscall 分支会多出来一个 user/trace.c 文件以及修改 conf/lab.mk

## Using gdb

第一部分是使用 gdb 来进行调试, 需要两个窗口, 读者可以开两个终端或者用tmux

第一个终端中执行, 使用 qemu 进行模拟并启用 gdb 调试映射到 26000 端口

```bash
make qemu-gdb
```

第二个终端中执行

```bash
gdb-multiarch
```

如果出现如下的警告信息, 则说明当前目录下的 .gdbinit 没有被正确加载

```bash
warning: File "/home/kamilu/MIT-6.S081/.gdbinit" auto-loading has been declined by your `auto-load safe-
path' set to "$debugdir:$datadir/auto-load".                                                            
To enable execution of this file add                                                                    
        add-auto-load-safe-path /home/kamilu/MIT-6.S081/.gdbinit                                        
line to your configuration file "/home/kamilu/.config/gdb/gdbinit".                                     
To completely disable this security protection add                                                      
        set auto-load safe-path /                                                                       
line to your configuration file "/home/kamilu/.config/gdb/gdbinit".                                     
For more information about this security protection see the                                             
"Auto-loading safe path" section in the GDB manual.  E.g., run from the shell:                          
        info "(gdb)Auto-loading safe path"
```

新建 `~/.config/gdb/gdbinit`, 并写入 `set auto-load safe-path /` 后重新执行, gdb 启动之时会优先执行 .gdbinit 当中的内容作为默认配置

```bash
set confirm off
set architecture riscv:rv64
target remote 127.0.0.1:26000
symbol-file kernel/kernel
set disassemble-next-line auto
set riscv use-compressed-breakpoints yes
```

接下来在 syscall 处打断点

```bash
(gdb) b syscall
(gdb) c
```

左侧停在了 `hart 2 starting hart 1 starting` 的输出信息

然后输入

```bash
(gdb) layout src
(gdb) backtrace
```

> layout src: 这个命令用于在 GDB 的文本界面中启用源代码布局.它会将源代码窗格显示在 GDB 界面的底部,以便你可以在调试过程中直接查看源代码.这对于单步调试时能够更清楚地了解程序的执行流程非常有帮助.
>
> backtrace: 这个命令用于查看函数调用堆栈(也称为堆栈回溯).它会显示当前程序执行的函数调用链,从当前位置一直追溯到程序的入口点.通过查看堆栈回溯,你可以了解函数是如何调用的,以及在哪些函数中出现了错误.

再输入c继续执行, 之后一直按回车即可(默认执行上一条continue), 可以在左侧看到 "init: starting sh" 提示信息一个字符一个字符的被打印出来

![20230803105048](https://raw.githubusercontent.com/learner-lu/picbed/master/20230803105048.png)

> 笔者对代码进行了格式化, 所以行号可能不同

**Q1: Looking at the backtrace output, which function called syscall?**

执行 backtrace 可以观察到上一个函数调用是位于 kernel/trap.c 下的 usertrap

![20230803155323](https://raw.githubusercontent.com/learner-lu/picbed/master/20230803155323.png)

```c
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
void usertrap(void) {

    // not important

    struct proc *p = myproc();

    // save user program counter.
    p->trapframe->epc = r_sepc();

    if (r_scause() == 8) {
        // system call

        if (killed(p))
            exit(-1);

        // sepc points to the ecall instruction,
        // but we want to return to the next instruction.
        p->trapframe->epc += 4;

        // an interrupt will change sepc, scause, and sstatus,
        // so enable only now that we're done with those registers.
        intr_on();

        syscall();
    }
    // not important
}
```

**Q2: What is the value of p->trapframe->a7 and what does that value represent?**

连续输入两次 n(next) 向下执行两句, 输入 `p/x *p` 可以看到 struct proc *p 结构体的值

> p 指 print, /x 指以 16 进制形式输出, *p 是前面代码中出现的 struct proc *p = myproc(); 变量

不过由于 p->trapframe 又是一个结构体, 这里只能看到地址, 所以输入 `p p->trapframe->a7` 即可看到结果为 7. 除此之外由于下面也使用了 `num = p->trapframe->a7;` 来进行赋值, 所以也可以再往下 n 一步然后查看 num, 如下图所示的两种方式

![20230803155849](https://raw.githubusercontent.com/learner-lu/picbed/master/20230803155849.png)

浏览 user/initcode.S 可以看到在此时为初始化阶段由用户空间执行系统调用 syscall , 对应的系统调用号为 SYS_exec

```riscvasm
# Initial process that execs /init.
# This code runs in user space.

#include "syscall.h"

# exec(init, argv)
.globl start
start:
        la a0, init
        la a1, argv
        li a7, SYS_exec
        ecall

# for(;;) exit();
exit:
        li a7, SYS_exit
        ecall
        jal exit
```

kernel/syscall.h 定义了 SYS_exec 和在 kernel/syscall.c 中定义的系统调用表, 对应第七位的 sys_exec 函数

```c
// System call numbers
#define SYS_fork    1
#define SYS_exit    2
#define SYS_wait    3
#define SYS_pipe    4
#define SYS_read    5
#define SYS_kill    6
#define SYS_exec    7
#define SYS_fstat   8
#define SYS_chdir   9
#define SYS_dup    10
#define SYS_getpid 11
#define SYS_sbrk   12
#define SYS_sleep  13
#define SYS_uptime 14
#define SYS_open   15
#define SYS_write  16
#define SYS_mknod  17
#define SYS_unlink 18
#define SYS_link   19
#define SYS_mkdir  20
#define SYS_close  21

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
typedef unsigned long uint64;
static uint64 (*syscalls[])(void) = {
    [SYS_fork] sys_fork,   [SYS_exit] sys_exit,     [SYS_wait] sys_wait,     [SYS_pipe] sys_pipe,
    [SYS_read] sys_read,   [SYS_kill] sys_kill,     [SYS_exec] sys_exec,     [SYS_fstat] sys_fstat,
    [SYS_chdir] sys_chdir, [SYS_dup] sys_dup,       [SYS_getpid] sys_getpid, [SYS_sbrk] sys_sbrk,
    [SYS_sleep] sys_sleep, [SYS_uptime] sys_uptime, [SYS_open] sys_open,     [SYS_write] sys_write,
    [SYS_mknod] sys_mknod, [SYS_unlink] sys_unlink, [SYS_link] sys_link,     [SYS_mkdir] sys_mkdir,
    [SYS_close] sys_close,
};
```

**Q3: What was the previous mode that the CPU was in?**

输入 `p /x $sstatus` 可以看到当前 sstatus 的值为 0x22, 或二进制的 0x100010

```bash
(gdb) p /x $sstatus
$1 = 0x22
(gdb) p /t $sstatus
$2 = 100010
```

根据提示浏览 [riscv-privileged-20211203.pdf](https://github.com/riscv/riscv-isa-manual/releases/download/Priv-v1.12/riscv-privileged-20211203.pdf), 可以在 4.1.1 Supervisor Status Register (sstatus) 找到 sstatus 的相关说明

> The sstatus register is an SXLEN-bit read/write register formatted as shown in Figure 4.1 when
SXLEN=32 and Figure 4.2 when SXLEN=64. The sstatus register keeps track of the processor's
current operating state.

sstatus 寄存器是一个 SXLEN 位读/写寄存器

> The SPP bit indicates the privilege level at which a hart was executing before entering supervisor
mode. When a trap is taken, SPP is set to 0 if the trap originated from user mode, or 1 otherwise

SPP 位表示在进入监控模式之前执行 hart 的权限级别位. 捕获陷阱时,如果陷阱来自用户模式,则 SPP 设为 0,否则设为 1.

因此, 此时查看得到的 sstatus 的值的 SPP 位为 0, 说明 trap 是从用户模式切换到内核模式造成的, 为一个系统调用的切换

![20230803173607](https://raw.githubusercontent.com/learner-lu/picbed/master/20230803173607.png)

**Q5: Write down the assembly instruction the kernel is panicing at. Which register corresponds to the varialable num?**

修改 syscall.c

```c
void syscall(void) {
    int num;
    struct proc *p = myproc();

    // num = p->trapframe->a7;
    num = * (int *) 0;
    if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        // Use num to lookup the system call function for num, call it,
        // and store its return value in p->trapframe->a0
        p->trapframe->a0 = syscalls[num]();
    } else {
        printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
        p->trapframe->a0 = -1;
    }
}
```

执行 make qemu 后报错, 提示出错位置为 `80001ff6`

```bash
hart 1 starting
hart 2 starting
scause 0x000000000000000d
sepc=0x0000000080001ff6 stval=0x0000000000000000
panic: kerneltrap
```

> 这里的值 80001ff6 可能不同

在 kernel/kernel.asm 中查找到此位置, 该汇编指令为 `lw a3, 0(zero)`, 也就是对应我们的修改 num = * (int *) 0;

```riscvasm
    80001ff6:	00002683          	lw	a3,0(zero) # 0 <_entry-0x80000000>
```

**Q6: Why does the kernel crash?**

在 `0x80001ff6` 打一个断点, 使用 c 运行到该处, 然后 n 单步执行一步, 触发恐慌之后使用 ctrl+c 中断. 使用 `p $scause` 打印造成恐慌的值

```bash
(gdb) b *0x80001ff6
Breakpoint 1 at 0x80001ff6: file kernel/syscall.c, line 109.
(gdb) c
Continuing.
[Switching to Thread 1.2]

Thread 2 hit Breakpoint 1, syscall () at kernel/syscall.c:109
109         num = * (int *) 0;
(gdb) n
^C
Thread 2 received signal SIGINT, Interrupt.
panic (s=s@entry=0x80008380 "kerneltrap") at kernel/printf.c:126
126       for(;;)
(gdb) p $scause
$1 = 13
```

查阅 [riscv-privileged-20211203.pdf](https://github.com/riscv/riscv-isa-manual/releases/download/Priv-v1.12/riscv-privileged-20211203.pdf) 4.1.9 章中提及到了 Supervisor cause register (scause) values after trap 的值, 找到 13 号对应加载页面失败

![20230803180520](https://raw.githubusercontent.com/learner-lu/picbed/master/20230803180520.png)

对于 xv6 书中图 3.3 中地址 0 位于 unused, 因此出现错误

![20230803180801](https://raw.githubusercontent.com/learner-lu/picbed/master/20230803180801.png)

> 左图中 KERNBASE 为内核空间起始地址 0x80000000

**Q7: What is the name of the binary that was running when the kernel paniced? What is its process id (pid)?**

重新在 syscall 打一个断点, 然后单步两次越过 struct proc *p 被赋值, 使用 p 打印 *p 的 name 和 pid, 可以看到是 initcode 和 1, 也就是 xv6 启动时候的第一个进程

```bash
(gdb) b syscall
(gdb) c
Continuing.
[Switching to Thread 1.3]

Thread 3 hit Breakpoint 1, syscall () at kernel/syscall.c:104
104     void syscall(void) {
(gdb) n
106         struct proc *p = myproc();
(gdb) n
109         num = * (int *) 0;
(gdb) p p->name
$1 = "initcode\000\000\000\000\000\000\000"
(gdb) p p->pid
$2 = 1
```

如果想使用 vscode + gdb 调试可以参考 [从零开始使用Vscode调试XV6](https://zhuanlan.zhihu.com/p/501901665), 关键信息是把 .gdbinit 中的 `target remote 127.0.0.1:26000` 注释掉

## trace

第二个实验的内容是新增一个系统调用 trace 以控制对其他系统调用的检查. user/trace.c 中实验已经提前为我们写好了 trace 程序的基本逻辑, 即将第二个参数作为参数传入 trace 系统调用. 后面的参数正常执行 exec 系统调用运行(有点类似 xarg)

> 将 $U/_trace 加入 Makefile

```c
#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int i;
    char *nargv[MAXARG];

    if (argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')) {
        fprintf(2, "Usage: %s mask command\n", argv[0]);
        exit(1);
    }

    if (trace(atoi(argv[1])) < 0) {
        fprintf(2, "%s: trace failed\n", argv[0]);
        exit(1);
    }

    for (i = 2; i < argc && i < MAXARG; i++) {
        nargv[i - 2] = argv[i];
    }
    exec(nargv[0], nargv);
    exit(0);
}
```

所以整体的思路就是 trace 系统调用的作用是为当前执行的进程设置一个 mask, 每次当 syscall 被调用的时候判断一下调用号与 mask 是否交叉, 并将信息输出

我们首先在 user/user.h 中添加对于 trace 函数的声明

```c
// system calls
int trace(int);
```

在 user/usys.pl 最后添加一个 trace 入口 `entry("trace");`, 此文件在 Makefile 中定义由 perl 解析到 usys.S

```Makefile
$U/usys.S : $U/usys.pl
	perl $U/usys.pl > $U/usys.S
```

user 部分的已经完成了, 接下来需要完善 kernel 部分, 首先在 kernel/syscall.h 中添加一个系统调用号

```c
#define SYS_trace  22
```

然后在 kernel/syscall.c 中添加一条系统调用 sys_trace, 并在 syscalls 数组中注册

```c
// Prototypes for the functions that handle system calls.
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_chdir(void);
extern uint64 sys_dup(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_uptime(void);
extern uint64 sys_open(void);
extern uint64 sys_write(void);
extern uint64 sys_mknod(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_close(void);
extern uint64 sys_trace(void);

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
typedef unsigned long uint64;
static uint64 (*syscalls[])(void) = {
    [SYS_fork] sys_fork,   [SYS_exit] sys_exit,     [SYS_wait] sys_wait,     [SYS_pipe] sys_pipe,
    [SYS_read] sys_read,   [SYS_kill] sys_kill,     [SYS_exec] sys_exec,     [SYS_fstat] sys_fstat,
    [SYS_chdir] sys_chdir, [SYS_dup] sys_dup,       [SYS_getpid] sys_getpid, [SYS_sbrk] sys_sbrk,
    [SYS_sleep] sys_sleep, [SYS_uptime] sys_uptime, [SYS_open] sys_open,     [SYS_write] sys_write,
    [SYS_mknod] sys_mknod, [SYS_unlink] sys_unlink, [SYS_link] sys_link,     [SYS_mkdir] sys_mkdir,
    [SYS_close] sys_close, [SYS_trace] sys_trace,
};
```

接下来需要实现 sys_trace. trace 系统调用本身需要做的事情只是为当前进程添加一个标记位 mask, 当执行其他的 syscall 时检查此标志位再做后续的输出处理, 因此我们需要修改位于 kernel/proc.h 的 proc 结构体, 为其添加一个 mask

```c
// Per-process state
struct proc {
  int mask;
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // wait_lock must be held when using this:
  struct proc *parent;         // Parent process

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};
```

然后在 kernel/sysproc.c 完成 sys_trace 的函数定义. 这里值得一提的是 trace 本身携带一个参数 `int trace(int);`, 但是系统调用 `sys_trace` 同其他所有系统调用是无法直接传参的, 这里是通过 `argint` 来获取参数信息, 然后将其赋值给 mask 变量, 最后为当前进程 p 设置 mask 值.

```c
uint64 sys_trace(void) {
    int mask = 0;
    argint(0, &mask);
    struct proc *p = myproc();
    p->mask = mask;
    return 0;
}
```

接下来修改 kernel/syscall.c, 首先创建一个数字 syscalls_name 用于记录所有系统调用的名字. 然后修改 syscall, 判断系统调用号是否包含在 mask 内 `(p->mask >> num) & 1`, 是则按照格式要求输出信息.

```c
// the system call name index array
static char *syscalls_name[] = {
    [SYS_fork] "syscall fork",   [SYS_exit] "syscall exit",     [SYS_wait] "syscall wait",
    [SYS_pipe] "syscall pipe",   [SYS_read] "syscall read",     [SYS_kill] "syscall kill",
    [SYS_exec] "syscall exec",   [SYS_fstat] "syscall fstat",   [SYS_chdir] "syscall chdir",
    [SYS_dup] "syscall dup",     [SYS_getpid] "syscall getpid", [SYS_sbrk] "syscall sbrk",
    [SYS_sleep] "syscall sleep", [SYS_uptime] "syscall uptime", [SYS_open] "syscall open",
    [SYS_write] "syscall write", [SYS_mknod] "syscall mknod",   [SYS_unlink] "syscall unlink",
    [SYS_link] "syscall link",   [SYS_mkdir] "syscall mkdir",   [SYS_close] "syscall close",
    [SYS_trace] "syscall trace",
};

void syscall(void) {
    int num;
    struct proc *p = myproc();

    num = p->trapframe->a7;
    if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        // Use num to lookup the system call function for num, call it,
        // and store its return value in p->trapframe->a0
        int a0 = syscalls[num]();
        if ((p->mask >> num) & 1) {
            printf("%d: %s -> %d\n", p->pid, syscalls_name[num], a0);
        }
        p->trapframe->a0 = a0;
    } else {
        printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
        p->trapframe->a0 = -1;
    }
}
```

到此为止已经完成了绝大部分的工作了, 但是还有一些小问题需要处理. 首先我们发现如果先执行 `trace 32 grep hello README` 再执行 `grep hello README` 仍然会有 trace 的输出, 这是因为 proc 在创建的时候初始状态下可能会有垃圾数据. 所以在 kernel/proc.c 修改 `allocproc`, 为 p 的 mask 手动置 0

```c
typedef unsigned long uint64;

static struct proc*
allocproc(void)
{
  // some code...

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  p->mask = 0;
}
```

接着我们发现执行最后一条 `trace 2 usertests forkforkfork` 的时候输出的信息很少, 所以需要修改下面的 fork 函数使得子进程可以继承父进程的 mask

```c
int
fork(void)
{
    // ...
    safestrcpy(np->name, p->name, sizeof(p->name));
    np->mask = p->mask;
    pid = np->pid;
    // ...
}
```

至此完成实验, 测试均通过

```bash
$ trace 32 grep hello README
3: syscall read -> 1023
3: syscall read -> 961
3: syscall read -> 321
3: syscall read -> 0
$ trace 2147483647 grep hello README
4: syscall trace -> 0
4: syscall exec -> 3
4: syscall open -> 3
4: syscall read -> 1023
4: syscall read -> 961
4: syscall read -> 321
4: syscall read -> 0
4: syscall close -> 0
$
$ grep hello README
$
$ trace 2 usertests forkforkfork
usertests starting
8: syscall fork -> 9
test forkforkfork: 8: syscall fork -> 10
10: syscall fork -> 11
...
14: syscall fork -> -1
11: syscall fork -> -1
OK
8: syscall fork -> 71
ALL TESTS PASSED
```

> 最终测试在 WSL2 下也通过了

```bash
make GRADEFLAGS=trace grade
```

---

我们借由 trace 来从代码角度整体回顾一下一个系统调用从用户态到内核态的执行流程. 

1. user/trace.c     用户编写了一段程序使用到了操作系统提供了一组系统调用中的 `int trace(int)`.
2. user/usys.S      trace 符号使用 `li a7, SYS_trace` 将 SYS_trace 的系统调用号放入 a7 寄存器, 然后使用 CPU 提供的 ecall 指令,调用到内核态 
3. kernel/syscall.c 通过 `p->trapframe->a7` 找到该系统调用号, `int a0 = syscalls[num]();` 通过系统调用编号,获取系统调用处理函数的指针,调用并将返回值存到用户进程的 a0 寄存器中
4. kernel/sysproc.c 执行对应的 sys_trace 函数, 使用 `argint(0, &mask)` 获取到传入的参数

## sysinfo

第三个实验是实现一个 sysinfo 系统调用, 获取当前系统的状态. 浏览 kernel/sysinfo.h 可以看到需要获取的信息是 freemem 和 nproc

```c
struct sysinfo {
  uint64 freemem;   // amount of free memory (bytes)
  uint64 nproc;     // number of process
};
```

前面的流程和实验二类似, 包括添加 Makefile usys.pl syscall 等等不再赘述, 重点来看一下实现. 浏览 user/sysinfotest.c 可以看到这个系统调用的参数是一个指针, 所以我们需要完成的是获取这个指针并为其结构体对应的位置填上对应的值

```c
void sinfo(struct sysinfo *info) {
    if (sysinfo(info) < 0) {
        printf("FAIL: sysinfo failed");
        exit(1);
    }
}
```

实现如下, 首先定义一个 64 位的 addr 用于获取指针, 使用 `argaddr` 函数拿到第一个参数的值, 即指针指向的地址. 然后定义结构体 `struct sysinfo` 并使用两个函数分别获取到对应的 nproc freemem 值. 最后使用 `copyout` 函数将 sizeof(info) 大小的内存块从 (char *)&info 拷贝到 addr, 完成对指针的赋值.

```c
#include "sysinfo.h"

uint64 sys_sysinfo(void) {
    uint64 addr;  // user pointer to struct sys_info
    argaddr(0, &addr);
    struct proc *p = myproc();
    struct sysinfo info;
    info.nproc = get_proc_number();
    info.freemem = get_free_mem();
    if(copyout(p->pagetable, addr, (char *)&info, sizeof(info)) < 0)
      return -1;
    return 0;
}
```

接下来需要实现 get_proc_number 和 get_free_mem 两个函数, 在 kernel/defs.h 中声明函数.

```c
// kalloc.c
uint64          get_free_mem(void);
// proc.c
uint64          get_proc_number(void);
```

结构体 struct proc 中的元素 enum procstate state 决定了当前进程的状态, 所以只需要遍历全局 `proc[NPROC]` 判断非 UNUSED 状态即 number++ 即可

```c
uint64 get_proc_number(void) {
    struct proc *p;
    int number = 0;
    for (p = proc; p < &proc[NPROC]; p++) {
        if (p->state != UNUSED) {
            number++;
        }
    }
    return number;
}
```

获取空闲内存需要先阅读一下 kalloc.c 的其他函数, 发现采用的是空闲链表头插法的方式, 每一个块都是固定大小 PGSIZE(4096), 所以只需要 while 遍历 freelist 即可, 同时注意一下需要上锁.

```c
uint64 get_free_mem(void) {
    acquire(&kmem.lock);
    uint64 mem_bytes = 0;
    struct run *r = kmem.freelist;
    while (r) {
        mem_bytes += PGSIZE;
        r = r->next;
    }
    release(&kmem.lock);
    return mem_bytes;
}
```

```bash
$ sysinfotest
sysinfotest: start
sysinfotest: OK
```

```bash
make GRADEFLAGS=sysinfo grade
```

## 参考

- [xv6-labs-2022-solutions](https://github.com/relaxcn/xv6-labs-2022-solutions)
- [miigon blog s081-lab2-system-calls](https://blog.miigon.net/posts/s081-lab2-system-calls/)