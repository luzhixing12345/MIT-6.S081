
#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main() {
    unsigned int i = 0x00646c72;
	printf("H%x Wo%s", 57616, &i);
    return 0;
}
