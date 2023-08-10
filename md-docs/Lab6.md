
# Lab6

> [实验文档](https://pdos.csail.mit.edu/6.828/2022/labs/thread.html)

多线程的实验对应 Chapter 7: Scheduling, 顺序稍有不同, 下一个实验 lab7 对应的是第五章的驱动

## Uthread: switching between threads

第一个实验的内容是模拟用户态线程的切换, 实验本身已经提供了绝大部分代码, 我们需要完成的是 thread_create thread_schedule 和 user/uthread_switch.S

首先对于 thread 结构体添加一个字段 context 用于记录上下文寄存器的信息, 这里直接使用 xv6 中的 context 结构体

```c
struct context {
    uint64 ra;
    uint64 sp;

    // callee-saved
    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
};

struct thread {
    char stack[STACK_SIZE]; /* the thread's stack */
    int state;              /* FREE, RUNNING, RUNNABLE */
    struct context context;
};
```

然后把 kernel/swtch.S 抄过来

```riscvasm
	.text
	.globl thread_switch
thread_switch:
    sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    ld ra, 0(a1)
    ld sp, 8(a1)
    ld s0, 16(a1)
    ld s1, 24(a1)
    ld s2, 32(a1)
    ld s3, 40(a1)
    ld s4, 48(a1)
    ld s5, 56(a1)
    ld s6, 64(a1)
    ld s7, 72(a1)
    ld s8, 80(a1)
    ld s9, 88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)
        
	ret
```

在线程创建阶段, 参考 kernel/proc.c 中 allocproc 时的代码

```c
// Set up new context to start executing at forkret,
// which returns to user space.
memset(&p->context, 0, sizeof(p->context));
p->context.ra = (uint64)forkret;
p->context.sp = p->kstack + PGSIZE;
```

设置空闲线程 t 的 state 为 RUNNABLE, 然后设置 ra 为回调函数 func 的首地址, 栈指针 sp 为栈的底部

```c
void thread_create(void (*func)()) {
    struct thread *t;

    for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
        if (t->state == FREE)
            break;
    }
    t->state = RUNNABLE;
    t->context.ra = (uint64)func;
    t->context.sp = (uint64)(t->stack + STACK_SIZE);
}
```

最后在调度的时候调用我们写好的 thread_switch 汇编代码, 传入进程的 context 元素位置; 首先将当前的几个寄存器状态保存在 t->context(原先的线程) 中, 然后将寄存器的值替换为现在将要执行的 current_thread->context, 接着从 ra 的位置开始执行

```c
void thread_schedule(void) {
    // ...
    if (current_thread != next_thread) { /* switch threads?  */
        next_thread->state = RUNNING;
        t = current_thread;
        current_thread = next_thread;
        thread_switch((uint64)&t->context, (uint64)&current_thread->context);
    } else
        next_thread = 0;
}
```

```bash
$ uthread
...
thread_c 99
thread_a 99
thread_b 99
thread_c: exit after 100
thread_a: exit after 100
thread_b: exit after 100
thread_schedule: no runnable threads
```

## Using threads

开始之前先提醒一下, ph.c 的代码存在很严重的内存泄漏, malloc 的都没有释放, 足足 16 * 100000 + 8 * nthread 大小, 笔者是真的难受的要死, 建议运行之前把 free 的代码添加在结尾

```c
int main(int argc, char *argv[]) {
    // 加在结尾
    free(tha);
    struct entry *e = 0;
    struct entry *next_e;
    for (int i = 0; i < NBUCKET; i++) {
        for (e = table[i]; e != 0;) {
            next_e = e->next;
            free(e);
            e = next_e;
        }
    }
}
```

先了解一下POSIX 线程库提供的两个函数,用于创建多个线程以实现在程序中并发执行多个任务

```c
int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
```

pthread_create 函数用于创建一个新线程,并将新线程的执行从当前位置开始.它会将新线程的标识符存储在 thread 指针指向的变量中.新线程会执行 start_routine 函数,传入 arg 作为参数.线程的属性可以通过 attr 参数进行设置.

- `thread`: 一个指向 pthread_t 类型变量的指针,用于存储新线程的标识符.
- `attr`: 一个指向 pthread_attr_t 类型的指针,表示线程的属性.可以传入 NULL 来使用默认属性.
- `start_routine`: 一个函数指针,指向新线程要执行的函数.该函数必须返回 void * 类型,接受一个 void * 类型的参数.
- `arg`: 传递给 start_routine 函数的参数.

pthread_join 函数用于等待指定的线程结束执行.如果线程还在运行,调用该函数的线程会被阻塞,直到指定的线程完成执行.如果传入的 retval 不为 NULL,它会被用来存储线程的返回值.

- `thread`: 要等待的线程的标识符.
- `retval`: 一个指向指针的指针,用于接收线程的返回值.如果不需要线程的返回值,可以传入 NULL.

多线程出现问题的主要原因就是在 put 函数上, 因为查找 entry 的方式是通过 for 循环遍历, 对于新键通过一个 mod5 的 HASH 找到对应的散列桶然后使用头插法, 因此如果先insert的线程还未返回另一个线程就开始insert那么就会出现数据的缺失

```c
static void put(int key, int value) {
    int i = key % NBUCKET;

    // is the key already present?
    struct entry *e = 0;
    for (e = table[i]; e != 0; e = e->next) {
        if (e->key == key)
            break;
    }
    if (e) {
        // update the existing key.
        e->value = value;
    } else {
        // the new is new.
        insert(key, value, &table[i], table[i]);
    }
}
```

因此只要给 insert 操作上一把锁就可以了

```c
pthread_mutex_t lock;

static void put(int key, int value) {
    int i = key % NBUCKET;

    // is the key already present?
    struct entry *e = 0;
    
    for (e = table[i]; e != 0; e = e->next) {
        if (e->key == key)
            break;
    }
    if (e) {
        // update the existing key.
        e->value = value;
    } else {
        // the new is new.
        pthread_mutex_lock(&lock);
        insert(key, value, &table[i], table[i]);
        pthread_mutex_unlock(&lock);
    }
}
int main(int argc, char *argv[]) {
    pthread_mutex_init(&lock, NULL);
    // ...
}
```

或者根据提示为每一个桶上一把锁

```c
pthread_mutex_t lock[NBUCKET] = { PTHREAD_MUTEX_INITIALIZER };
static void put(int key, int value) {
    int i = key % NBUCKET;

    // is the key already present?
    struct entry *e = 0;
    
    for (e = table[i]; e != 0; e = e->next) {
        if (e->key == key)
            break;
    }
    if (e) {
        // update the existing key.
        e->value = value;
    } else {
        // the new is new.
        pthread_mutex_lock(&lock[i]);
        insert(key, value, &table[i], table[i]);
        pthread_mutex_unlock(&lock[i]);
    }
}
```

> 全局一把锁和每个桶一把锁, 两种方法都可以通过

```bash
make grade GRADEFLAGS=ph_safe
make grade GRADEFLAGS=ph_fast
```

## Barrier

> 同样, 先 free(tha);

```c
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_broadcast(pthread_cond_t *cond);
```

pthread_cond_wait 函数用于阻塞调用线程,直到条件变量的状态发生变化.为了使用 pthread_cond_wait,通常的做法是先获取一个互斥锁(mutex),然后调用这个函数.函数会将调用线程阻塞,同时释放 mutex,以便其他线程可以访问共享资源.一旦条件变量(cond)发生变化,线程会被唤醒并重新获取 mutex,继续执行.

- `cond`: 指向条件变量的指针.
- `mutex`: 指向互斥锁的指针.

pthread_cond_broadcast 函数用于广播给定的条件变量,唤醒所有正在等待此条件变量的线程.这允许多个线程同时获得互斥锁并继续执行,通常用于在一组等待线程中选择一个或多个来继续执行,以响应某个事件.

thread 中的断言条件是 assert(i == t); 也就是说每个线程在调用 barrier 之后都要先停下, 直到所有 n 个线程都调用了 barrier 之后将 round + 1, 然后唤醒所有线程

```c
static void barrier() {
    pthread_mutex_lock(&bstate.barrier_mutex);
    bstate.nthread++;
    if (bstate.nthread == nthread) {
        bstate.round++;
        bstate.nthread = 0;
        pthread_cond_broadcast(&bstate.barrier_cond);
    } else {
        pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
    }
    pthread_mutex_unlock(&bstate.barrier_mutex);
}
```

```bash
make barrier
./barrier 2
./barrier 10
```

## 参考

- [11.7 XV6线程切换 --- switch函数](https://zhuanlan.zhihu.com/p/346709521)
- [【CSAPP】背景知识-调用者保存寄存器与被调用者保存寄存器](https://blog.csdn.net/Edidaughter/article/details/122334074)