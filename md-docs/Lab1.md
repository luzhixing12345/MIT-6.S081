
# Lab1: Xv6 and Unix utilities

第一个实验所在的默认分支就是 util, 所以不需要调整

> 实验文档: [xv6 lab1 utils](https://pdos.csail.mit.edu/6.828/2022/labs/util.html)

其中 user/ 目录下为用户编写的一个个程序, `user/user.h` 提供了可以使用的一些系统调用和C标准库函数, user/user.h 中的 uint 并不是 C 标准规定的关键字, 通常为编译器GCC扩展, 所以需要使用 `kernel/types.h` 中的定义

> 这里比较难受的是头文件的顺序不能改变, 因为 `user/user.h` 的函数定义依赖了 `kernel/types.h` 中的定义

## sleep

新建 user/sleep.c

```c
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  if (argc != 2) { //参数错误
    fprintf(2, "usage: sleep <time>\n");
    exit(1);
  }
  sleep(atoi(argv[1]));
  exit(0);
}
```

先判断一下参数个数, 如果出错使用 fprintf 将错误信息输出到 stderr, 使用系统调用 sleep 进入睡眠, 使用标准库函数 atoi 将 char * 转为 int

编写完代码之后还需要修改 Makefile, 在 UPROGS 中添加一个命令叫 `_sleep`

```Makefile
UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_sleep
```

对于用户程序会由编译器编译得到以 `_` 开头的可执行文件, 导出符号表, 最后由 mkfs/mkfs 统一打包到 `fs.img` 镜像中

```Makefile
_%: %.o $(ULIB)
	$(LD) $(LDFLAGS) -T $U/user.ld -o $@ $^
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym
```

> %.o 通过 Makefile 的隐式规则由 %.c 推导. 下面的所有任务都将对应的可执行文件名加入 `UPROGS` 中, 不再赘述

添加完成后可以使用如下命令测试正确性, 其中 make grade 测试当前 LAB 的全部任务, 这里需要指定 `GRADEFLAGS` 为 sleep

> LAB 定义在 conf/lab.mk

```bash
make GRADEFLAGS=sleep grade
```

![20230710152135](https://raw.githubusercontent.com/learner-lu/picbed/master/20230710152135.png)

同时注意到时钟周期是 xv6 内核定义的时间概念,即定时器芯片两次中断之间的时间,所以运行时间会比实际时间短

## pingpong

```c
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    
    int pipefd_p2c[2];
    int pipefd_c2p[2];

    pipe(pipefd_p2c);
    pipe(pipefd_c2p);

    char buf = 'P';

    // pipe 0 是读; 1 是写
    int pid = fork();
    if (pid == 0) {
        // child process
        close(pipefd_c2p[0]);
        close(pipefd_p2c[1]);
        read(pipefd_p2c[0], &buf, 1);
        printf("%d: received ping\n", getpid());
        write(pipefd_c2p[1], &buf, 1);
        close(pipefd_c2p[1]);
        close(pipefd_p2c[0]);
        exit(0);
    } else if (pid > 0) {
        close(pipefd_p2c[0]);
        close(pipefd_c2p[1]);
        write(pipefd_p2c[1], &buf, 1);
        read(pipefd_c2p[0], &buf, 1);
        printf("%d: received pong\n", getpid());
        close(pipefd_p2c[1]);
        close(pipefd_c2p[0]);
        exit(0);
    }
    exit(0);
}
```

pingpong 的代码本身比较清晰, pipe 系统调用仅仅创造一个管道, 只需要 fork 并将父子进程的读写分别连接管道的一端, 完成两次 read write 即可

> 这里的代码略显简陋, 如果考虑全面的话就加上返回值的判断, pipe 失败关闭 fd, write read 失败.. 这里只把最关键的逻辑写出来了

xv6 中测试

```bash
$ pingpong
4: received ping
3: received pong
```

运行测试

```bash
make GRADEFLAGS=pingpong grade
```

## primes

```c
#include "kernel/types.h"
#include "user/user.h"

void prime(int pipe_fd[2]) {
    int first_number;
    close(pipe_fd[1]);
    if (read(pipe_fd[0], &first_number, sizeof(int)) == sizeof(int)) {
        printf("prime %d\n",first_number);
        int data;
        int fd[2];
        pipe(fd);
        while (read(pipe_fd[0], &data, sizeof(int)) == sizeof(int)) {
            if (data%first_number) {
                write(fd[1], &data, sizeof(data));
            }
        }
        close(pipe_fd[0]); // 关闭子进程的 pipe_fd 的读写
        int pid = fork();
        if (pid == 0) {
            prime(fd);
        } else {
            close(fd[0]);
            close(fd[1]);
            wait((int*)0);
        }
    }
}


int main(int argc, char *argv[]) {
    int pipe_fd[2];
    pipe(pipe_fd);
    for (int i = 2; i <= 35; i++) {
        write(pipe_fd[1], &i, sizeof(i));
    }
    int pid = fork();
    if (pid == 0) {
        prime(pipe_fd);
    } else {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        wait((int*)0);
    }
    exit(0);
}
```

实验说明部分有一些含糊, [Bell Labs and CSP Threads](https://swtch.com/~rsc/thread/) 给出的一幅图比较直观

![20230710222957](https://raw.githubusercontent.com/learner-lu/picbed/master/20230710222957.png)

素数的判断方法就是对 (2 - n-1) 的数相除, 这种管道判断的方法是指第一次将 2-35 所有的数传递给子进程, 子进程获取第一个素数 2, 然后遍历判断获取到的所有的数是否可以被2整除,对于不能整除的传递给子进程,以此类推

这种方式的好处在于相当于逐次剔除掉可以被整除的数

值得一提的是代码中需要注意关闭 pipe 的时机, 对于父进程来说向管道 write 了所有不可被整除的数就可以关闭 pipefd 了. 对于子进程需要在内部再创建一个管道 fd, fork 之后将 fd 交由子进程继续处理, 自己关闭 fd

![20230711090913](https://raw.githubusercontent.com/learner-lu/picbed/master/20230711090913.png)

```bash
make GRADEFLAGS=primes grade
```

## find

```c
#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

char *fmtname(char *path) {
    static char buf[DIRSIZ + 1];
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
    return buf;
}

void find(char *path, char *name) {
    char buf[512], *p;
    int fd;
    struct dirent dir;
    struct stat st;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    if (st.type != T_DIR) {
        fprintf(2, "usage: find <DIRECTORY> <filename>\n");
        return;
    }

    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while (read(fd, &dir, sizeof(dir)) == sizeof(dir)) {
        if (dir.inum == 0)
            continue;
        memmove(p, dir.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if (stat(buf, &st) < 0) {
            printf("find: cannot stat %s\n", buf);
            continue;
        }
        if (st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0) {
            // 递归搜索除 . .. 外的子目录
            find(buf, name);
        } else if (strcmp(name, p) == 0) {
            printf("%s\n", buf);
        }
    }

    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(2, "usage: find <DIRECTORY> <filename>\n");
    }
    find(argv[1], argv[2]);
    exit(0);
}
```

> user/ls.c 中给出了一份比较好的示例代码, 其中对于目录的操作, 对于文件信息的读取都给出了相应的函数调用, 可以在此基础上修改代码

首先判断一下当前 path 是目录 `T_DIR`, 对于目录下的所有文件如果是目录并且不是 . ..,则递归执行 find, 如果是文件则使用 strcmp 判断一下是否是 name

find 正常来说应该支持正则查找, 这里简化了一下只是用 strcmp 做文件名相同的判断

```bash
$ find . b
./b
./a/b
./a/aa/b
```

```bash
make GRADEFLAGS=find grade
```

> 注意执行之后使用 make clean 以获得干净的文件系统

## xargs

```c
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[]) {
    if (argc - 1 >= MAXARG) {
        fprintf(2, "xargs: too many arguments.\n");
        exit(1);
    } else if (argc < 2) {
        fprintf(2, "xargs: at lease 2 arguments.\n");
        exit(1);
    }
    char *x_argv[MAXARG] = {0};

    int total_args_number = 0;
    // 存储原有的参数
    for (int i = 1; i < argc; ++i) {
        x_argv[i - 1] = argv[i];
    }
    total_args_number = argc - 1;

    // 将从上游传来的输出读取到 buf 中
    char buf[1024];
    int index = 0;
    while (read(0, buf + index, sizeof(char))) {
        index++;
    }
    buf[index] = 0;
    int buffer_length = index;
    int i = 0;
    while (i < buffer_length) {
        // 跳过空格
        if (buf[i] == ' ') {
            i++;
        } else {
            // 对于非空格字符, 记录其首地址
            x_argv[total_args_number++] = &buf[i];
            if (total_args_number > MAXARG) {
                printf("to many arguments\n");
                exit(1);
            }
            // 一直匹配, 直到遇到空格或换行
            while (i < buffer_length && buf[i] != ' ' && buf[i] != '\n') {
                i++;
            }
            // 将 buf 中字符串结尾改为 0 终止
            if (i < buffer_length) {
                buf[i] = 0;
                i++;
            }
        }
    }
    // fork 一个子进程执行 xargs 第一个参数对应的指令
    if (fork() == 0) {
        exec(x_argv[0], x_argv);
    } else {
        wait(0);
    }
    exit(0);
}

```

xargs 单独使用比较少, 一般配合管道联合 grep 对前面程序的输出做处理. 实现思路也比较清晰, 本身 sh 帮助我们完成了管道的数据传递, 使用 `read` 从 stdin 不断地读取输入即可. 接着对于空格和换行符做分割, 将结果保存在 xargv 数组中, 最后使用 fork 在子进程中执行调用

```bash
$ sh < xargstest.sh
$ $ $ $ $ $ hello
hello
hello
$ $
```

```bash
make GRADEFLAGS=xargs grade
# == Test xargs == xargs: OK (2.6s)
```

## Optional challenge exercises

### uptime

uptime 的内容比较简单, 只需要调用提供的用户态 uptime 函数即可

```c
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char**argv) {
    int time = uptime();
    printf("%d\n",time);
    exit(0);
}
```

```bash
$ uptime
33
$ uptime
67
$ uptime
105
```

### better find

要求在名称匹配中支持正则表达式, `grep.c` 中提供了一个简单的正则匹配函数 `match`, 只需要将其迁移过来, 并且将 find 中原先的 `strcmp` 替换为 `match` 即可. 见 [commit](https://github.com/luzhixing12345/MIT-6.S081/commit/a8e0a84c3fa0d288ef856a95d7eeabab2181d447)

```bash
$ find . e$
./zombie
./uptime
./console
$ find . ^s
./sh
./stressfs
./sleep
```

> 给出的 match 所支持的正则不多, 这里简单匹配一下以 `e` 结尾的和以 `s` 开头的

### better sh

要求改进一下当前的 sh.c 以获取一个更好的 shell 体验

> 这个不太想做了, 之前 csapp 里已经写过一次 shell lab 了, 虽然功能很差劲不过先这样吧

## 总结

相较来说第一次实验还是一次相当基础的用户态程序的实现, 就是去熟悉一下由 xv6 提供的 Linux 头文件和 C 标准库的函数, 在此基础上编写实现一个小功能函数

了解基本运行方式和测试方式之后就可以完成后续的实验了