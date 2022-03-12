#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void prime(int p[]) {
    close(p[1]);   // 关闭写段;

    int first;
    int n = read(p[0], &first, 4);
    if(n == 0) {
        close(p[0]);
        exit(0);
    }
    
    fprintf(1, "prime %d\n", first);

    int pp[2];   // 新开一个管道
    pipe(pp);

    int pid = fork();
    if(pid == 0) {
        close(p[0]);
        prime(pp);
        // exit(0); 
    }

    else {
        int pr;
        close(pp[0]);
        while(read(p[0], &pr, 4) == 4) {
            if(pr % first != 0) {
                write(pp[1], &pr, 4);
            }
        }
        
        close(pp[1]);
        close(p[0]);
        wait(0);
    }

    exit(0);
}

int main(void) {
    int p[2];
    pipe(p);   // 创建一个管道;
    
    int pid = fork();
    if(pid == 0) {
        prime(p);
    } else {
        close(p[0]);  // 把读端关了;
        for(int i = 2; i <= 35; i++) {
            write(p[1], &i, 4);
        }
        close(p[1]);
        wait(0);   // 这里一定要wait!!!
    }
    
    // fprintf(1, "success exit!");
    exit(0);
}