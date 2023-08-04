
# Lab2

> [lab2 syscall](https://pdos.csail.mit.edu/6.828/2022/labs/syscall.html)

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

```x86asm
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
typedef int uint64;
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

```asm
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

## 参考

- [xv6-labs-2022-solutions](https://github.com/relaxcn/xv6-labs-2022-solutions)