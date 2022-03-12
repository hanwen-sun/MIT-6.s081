#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"
#include "../kernel/param.h"

#define STD_IN 0
#define STD_OUT 1
#define STD_ERR 2

int main(int argc, char *argv[]){
    if (argc < 2){
        fprintf(STD_ERR, "Usage: xargs<arguments>\n");
        exit(1);
    }
    if (argc + 1 > MAXARG){
        fprintf(STD_OUT, "Too many arguments!\n");
        exit(1);
    }

    char buf[512];
    char *params[MAXARG];
    int i;
    for (i = 1; i < argc; i++) params[i - 1] = argv[i];
    for (;;){
        i = 0;
        for (;;){
            int n = read(STD_IN, &buf[i], 1);
            if (n == 0 || buf[i] == '\n') break;
            i++;
        }
        if (i == 0) break;
        buf[i] = '\0';
        params[argc - 1] = buf;
        if (fork() == 0){
            exec(argv[1], params);
        }
        else {
            wait(0);
        }
    }
    exit(0);

}