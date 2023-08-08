
# Lab4

> [实验文档](https://pdos.csail.mit.edu/6.828/2022/labs/traps.html)

```bash
git checkout traps
make clean
```

## RISC-V assembly

第一个实验的要求是阅读 RISCV 的汇编代码, 内容相对比较简单, 下面直接给出答案

```bash
make fs.img
```

user/call.c 相关代码如下

```c
int g(int x) {
  return x+3;
}

int f(int x) {
  return g(x);
}

void main(void) {
  printf("%d %d\n", f(8)+1, 13);
  exit(0);
}
```

汇编结果如下

```riscvasm
0000000000000000 <g>:

int g(int x) {
   0:	1141                	addi	sp,sp,-16
   2:	e422                	sd	s0,8(sp)
   4:	0800                	addi	s0,sp,16
  return x+3;
}
   6:	250d                	addiw	a0,a0,3
   8:	6422                	ld	s0,8(sp)
   a:	0141                	addi	sp,sp,16
   c:	8082                	ret

000000000000000e <f>:

int f(int x) {
   e:	1141                	addi	sp,sp,-16
  10:	e422                	sd	s0,8(sp)
  12:	0800                	addi	s0,sp,16
  return g(x);
}
  14:	250d                	addiw	a0,a0,3
  16:	6422                	ld	s0,8(sp)
  18:	0141                	addi	sp,sp,16
  1a:	8082                	ret

000000000000001c <main>:

void main(void) {
  1c:	1141                	addi	sp,sp,-16
  1e:	e406                	sd	ra,8(sp)
  20:	e022                	sd	s0,0(sp)
  22:	0800                	addi	s0,sp,16
  printf("%d %d\n", f(8)+1, 13);
  24:	4635                	li	a2,13
  26:	45b1                	li	a1,12
  28:	00000517          	auipc	a0,0x0
  2c:	7b850513          	addi	a0,a0,1976 # 7e0 <malloc+0xe6>
  30:	00000097          	auipc	ra,0x0
  34:	612080e7          	jalr	1554(ra) # 642 <printf>
  exit(0);
  38:	4501                	li	a0,0
  3a:	00000097          	auipc	ra,0x0
  3e:	28e080e7          	jalr	654(ra) # 2c8 <exit>
```

**Q1: Which registers contain arguments to functions? For example, which register holds 13 in main's call to printf?**

riscv 使用通用寄存器 a0 - a7, 多余参数保存在栈中

代码中一共出现了四次函数调用, 分别是 printf f g exit, 可以看到 0x24 使用 a2 寄存器保存了 13, 编译器将 f(8) + 1 的函数调用和运算直接进行优化在编译期计算得到 12 并使用 a1 保存, 0x38 处使用 a0 保存 exit 的参数 0

**Q2: Where is the call to function f in the assembly code for main? Where is the call to g?**

main 的汇编代码没有调用 f 和 g 函数.编译器对其进行了优化

**Q3: At what address is the function printf located?**

`0x642 <printf>`

**Q4: What value is in the register ra just after the jalr to printf in main?**

`0x38`

**Q5: What is the output? The output depends on that fact that the RISC-V is little-endian. If the RISC-V were instead big-endian what would you set i to in order to yield the same output? Would you need to change 57616 to a different value?**

可以创建一个文件 user/a.c 并加入 Makefile, 在其中执行, 输出结果为 "HE110 World"

```c
#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    unsigned int i = 0x00646c72;
	printf("H%x Wo%s", 57616, &i);
    return 0;
}
```

57616 使用 %x 输出 16 进制为 `0xe110`. 小端存储的 i 低字节在低地址处, 使用 %s 将其作为字符串输出, 0x72 0x6c 0x64 对应的 ASCII 字符分别为 r l d, 所以最终结果为 `HE110 World`. 大端存储则修改 i 值为 `0x726c6400`, 无论 57616 在大端序还是小端序,它的二进制值都为 e110 .大端序和小端序只是改变了多字节数据在内存中的存放方式,并不改变其真正的值的大小,所以 57616 始终打印为二进制 e110

**In the following code, what is going to be printed after 'y='?**

```c
printf("x=%d y=%d", 3);
```

不确定, 由 a1 寄存器调用之前的值决定

## Backtrace

第二个实验的任务是完成对于函数调用栈的信息输出

在 kernel/defs.h 声明 backtrace

```c
// printf.c
void            printf(char*, ...);
void            panic(char*) __attribute__((noreturn));
void            printfinit(void);
void            backtrace(void);
```

在 kernel/riscv.h 中定义 r_fp

```c
static inline uint64
r_fp()
{
  uint64 x;
  asm volatile("mv %0, s0" : "=r" (x) );
  return x;
}
```

在 kernel/sysproc.c 的 sys_sleep 中调用 backtrace

```c
uint64 sys_sleep(void) {
    int n;
    uint ticks0;
    backtrace();

    argint(0, &n);
    if (n < 0)
        n = 0;
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n) {
        if (killed(myproc())) {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}
```

在 kernel/printf.c 中实现 backtrace

```c
void backtrace() {
    uint64 fp = r_fp();
    while (fp != PGROUNDDOWN(fp)) {       // 判断触底
        uint64 ra = *(uint64 *)(fp - 8);  // return address
        printf("%p\n", ra);
        fp = *(uint64 *)(fp - 16);  // previous fp
    }
}
```

RISCV 的函数调用栈如下所示, 其中 fp 为当前栈帧, fp-8 保存着返回地址, fp-16 保存着上一个栈帧的位置, 因此首先使用 r_fp 拿到当前 bracetrace 函数的栈帧, 接着打印返回地址 ra 的值, 并将 fp 置为前一个栈帧 fp-16 位置的值, 如此循环. 使用 PGROUNDDOWN 来判断是否触底

![20230808153053](https://raw.githubusercontent.com/learner-lu/picbed/master/20230808153053.png)

> [lecture notes](https://pdos.csail.mit.edu/6.1810/2022/lec/l-riscv.txt)

运行结果如下, 使用 addr2line 可以对应调用行数

```bash
$ bttest
0x000000008000212e
0x0000000080002020
0x0000000080001d16
```

```bash
(base) kamilu@LZX:~/xv6-labs-2022$ addr2line -e kernel/kernel
0x000000008000212e
/home/kamilu/xv6-labs-2022/kernel/sysproc.c:47
0x0000000080002020
/home/kamilu/xv6-labs-2022/kernel/syscall.c:141
0x0000000080001d16
/home/kamilu/xv6-labs-2022/kernel/trap.c:76
```

> 顺便可以加入到 panic 之中在异常之时打印函数调用栈

## Alarm

最后一个实验的要求是添加两个系统调用, 用于定时记录进程的运行时间.

> 添加系统调用的方式同前面相同, 这里不再赘述

### test0

我们先来看一下比较简单的 test0, 实现 alarm 的基础功能. 首先在 kernel/proc.h 为 proc 结构体添加三个元素, 分别记录时间间隔 interval, 已经经过的时间 passed_ticks, 和需要执行的函数 handler

```c
struct proc {
    struct spinlock lock;

    // p->lock must be held when using these:
    enum procstate state;  // Process state
    void *chan;            // If non-zero, sleeping on chan
    int killed;            // If non-zero, have been killed
    int xstate;            // Exit status to be returned to parent's wait
    int pid;               // Process ID
    int interval;
    int passed_ticks;
    void (*handler)();
};
```

kernel/proc.c 在进程初始化和释放的过程中完成初始化和收尾工作.

```c
static struct proc *allocproc(void) {
    // ...
    p->passed_ticks = 0;
    p->handler = (void *)0;
    p->interval = 0;
    // ...
}

static void freeproc(struct proc *p) {
    // ...
    p->interval = 0;
    p->handler = 0;
    p->passed_ticks = 0;
    // ...
}
```

在 kernel/sysproc.c 的 sys_sigalarm 中读取参数完成对于 p 的赋值

```c
uint64 sys_sigalarm(void) {
    int interval;
    uint64 handler;
    argint(0, &interval);
    argaddr(1, &handler);
    struct proc *p = myproc();
    p->interval = interval;
    p->handler = (void *)handler;
    p->passed_ticks = 0;

    return 0;
}

uint64 sys_sigreturn(void) {
    return 0;
}
```

程序执行执行系统调用后返回到用户空间的完整过程如下

- ecall指令中将PC保存到SEPC
- 在usertrap中将SEPC保存到p->trapframe->epc
- p->trapframe->epc加4指向下一条指令
- 执行系统调用
- 在usertrapret中将SEPC改写为p->trapframe->epc中的值
- 在sret中将PC设置为SEPC的值

因此需要在 kernel/trap.c 中修改 usertrap, 对于 devintr 返回值为 2, 即定时器中断的情况. 首先检查是否设置了时间间隔, 若是, 则将 passed_ticks + 1, 检查是否到达. 将当前进程 trapframe 的 epc 值设置为当前需要执行的 handler 的地址, 结束时间片中断后由此处开始继续执行

```c
if (r_scause() == 8) {
    // ...
} else if ((which_dev = devintr()) != 0) {
    // ok
    if (which_dev == 2) {
        if (p->interval != 0) {
            p->passed_ticks += 1;
            if (p->passed_ticks == p->interval) {
                p->trapframe->epc = (uint64)p->handler;
                p->passed_ticks = 0;
            }
        }
    }
} else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
}
```

至此就可以成功通过 test0 了, 但是注意到其余 test 均失败了

```bash
$ alarmtest
test0 start
.........alarm!
test0 passed
test1 start
.alarm!
.alarm!
..alarm!
..alarm!
..alarm!
..alarm!
...alarm!
.alarm!
.alarm!
..alarm!

test1 failed: foo() executed fewer times than it was called
�"�usertrap(): unexpected scause 0x000000000000000c pid=3
            sepc=0x0000000000014f50 stval=0x0000000000014f50
```

### 完整实现

要解决的主要问题是寄存器保存恢复和防止重复执行的问题. 因为此时我们设置了 `p->trapframe->epc = (uint64)p->handler`, 从处理函数handler的地址开始继续执行, 而handler可能会改变用户寄存器

所以要在usertrap中再次保存用户寄存器,当handler调用sigreturn时将其恢复,并且要防止在handler执行过程中重复调用

添加 alarm_trapframe 用于保存当前陷阱帧状态, 以及 is_alarming 用于判断当前是否已经有了正在判断的正

```c
struct proc {
    // ...
    int interval;
    int passed_ticks;
    void (*handler)();
    struct trapframe *alarm_trapframe;
    int is_alarming;
    // ...
};
```

完成对于 alarm_trapframe 和 is_alarming 的初始化和释放, 注意对于 trapframe 采用 kalloc 和 kfree 进行分配与释放

```c
static struct proc *allocproc(void) {
    // ...
    if ((p->alarm_trapframe = (struct trapframe *)kalloc()) == 0) {
        release(&p->lock);
        return 0;
    }
    p->passed_ticks = 0;
    p->handler = (void *)0;
    p->interval = 0;
    p->is_alarming = 0;
    // ...
}
static void freeproc(struct proc *p) {
    // ...
    if (p->alarm_trapframe)
        kfree((void *)p->alarm_trapframe);
    p->alarm_trapframe = 0;
    p->interval = 0;
    p->handler = 0;
    p->passed_ticks = 0;
    p->is_alarming = 0;
    // ...
}
```

trap.c 中在条件判断中增加 `!p->is_alarming` 用于确定当前没有其他 alarm 影响, 不然会导致重复覆盖 alarm_trapframe 页面. 接着使用 memmove 将 trapframe 页面复制到 alarm_trapframe 做备份, 后将 trapframe 的 epc 设置为 handler 的地址开始执行.

```c
if (r_scause() == 8) {
    // ...
} else if ((which_dev = devintr()) != 0) {
    // ok
    if (which_dev == 2) {
        if (p->interval != 0) {
            p->passed_ticks += 1;
            if (p->passed_ticks == p->interval && !p->is_alarming) {
                memmove(p->alarm_trapframe, p->trapframe, sizeof(struct trapframe));
                p->trapframe->epc = (uint64)p->handler;
                p->passed_ticks = 0;
                p->is_alarming = 1;
            }
        }
    }
} else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
}
```

系统调用中 sys_sigalarm 没有变化, sys_sigreturn 将 trapframe 通过 alarm_trapframe 进行恢复, 情况 is_alarming, 返回值设置为先前 trapframe 的 a0

```c
uint64 sys_sigalarm(void) {
    int interval;
    uint64 handler;
    argint(0, &interval);
    argaddr(1, &handler);
    struct proc *p = myproc();
    p->interval = interval;
    p->handler = (void *)handler;
    p->passed_ticks = 0;

    return 0;
}

uint64 sys_sigreturn(void) {
    struct proc *p = myproc();
    memmove(p->trapframe, p->alarm_trapframe, sizeof(struct trapframe));
    p->is_alarming = 0;
    return p->trapframe->a0;
}
```