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
