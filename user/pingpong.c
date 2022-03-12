#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void) {
	int parent[2];   // 父进程的pipe参数;
	int child[2];    // 子进程的pipe参数;
	pipe(parent);    // 创建两个pipe;
    pipe(child);
    
    char buf[1];
    
    int pid = fork();
    if(pid == 0) {  // 子进程;
        close(parent[0]);  // 关闭父通道的读;
        close(child[1]);   // 关闭子通道的写;
        int n = read(child[0], buf, 1);   // 读一个字节;
        if(n)
        	fprintf(1, "%d: received ping\n", getpid());
        else
            exit(1);       // 异常退出;
        close(child[0]);   // 关闭对父通道的读;
        write(parent[1], buf, 1);  // 将一个字节发送给父进程;
        close(child[1]);   // 关闭子进程的写;
        exit(0);
    }
    else if(pid > 0) {   // 父进程;
        close(parent[1]);
        close(child[0]);
        write(child[1], buf, 1);
        close(child[1]);
        wait(0);
        int n = read(parent[0], buf, 1);
        if(n) {
            fprintf(1, "%d: received pong\n", getpid());
            close(parent[0]);
		}
    }else {
        fprintf(2, "fork error!");
        exit(1);
    }
    exit(0);
}