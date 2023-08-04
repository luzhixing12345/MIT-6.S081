
#include "kernel/types.h"
#include "user/user.h"

void prime(int pipe_fd[2]) {
    int first_number;
    close(pipe_fd[1]);
    if (read(pipe_fd[0], &first_number, sizeof(int)) == sizeof(int)) {
        printf("prime %d\n", first_number);
        int data;
        int fd[2];
        pipe(fd);
        while (read(pipe_fd[0], &data, sizeof(int)) == sizeof(int)) {
            if (data % first_number) {
                write(fd[1], &data, sizeof(data));
            }
        }
        close(pipe_fd[0]);  // 关闭子进程的 pipe_fd 的读写
        int pid = fork();
        if (pid == 0) {
            prime(fd);
        } else {
            close(fd[0]);
            close(fd[1]);
            wait((int *)0);
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
        wait((int *)0);
    }
    exit(0);
}