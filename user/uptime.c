#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char**argv) {
    int time = uptime();
    printf("%d\n",time);
    exit(0);
}