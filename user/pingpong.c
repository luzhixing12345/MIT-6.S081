
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