#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *fmtname(char *path){
    static char buf[DIRSIZ + 1];
    char *p;
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;
    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p)); 
    buf[strlen(p)] = '\0';   // 特别注意， 这里与ls中的不同;
    return buf;
}

void find(char *DIR, char *file_name) {
  // fprintf(1, "now the path is: %s, file_name is: %s\n", DIR, file_name);
  char buf[512], *p;
  int fd;
  struct dirent de;  // 目录结构体, inum 该目录下文件数, name 文件名;
  struct stat st;  // 文件结构体，其中type表示文件类型;

  // fprintf(1, "path is %s", path);
  if((fd = open(DIR, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", DIR);
    return;
  }

  if(fstat(fd, &st) < 0){      // 将fd指向的文件指针存入结构体st中;
    fprintf(2, "find: cannot stat %s\n", DIR);
    close(fd);
    return;
  }
  
  switch(st.type) {
    case T_FILE:      // 当前文件为文件;
        // fprintf(1, "fmtname(DIR) is %s\n", fmtname(DIR));
        // fprintf(1, "%d\n", strcmp(file_name, fmtname(DIR)));
        if(strcmp(file_name, fmtname(DIR)) == 0) {
            fprintf(1, "%s\n", DIR);
        }
        break;
    case T_DIR:
        if(strlen(DIR) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("ls: path too long\n");
      break;
    }
    strcpy(buf, DIR);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf("ls: cannot stat %s\n", buf);
        continue;
      }
      // 到这里得到当前目录的一个下级目录;
      // fprintf(1, "buf = %s, fmtname(buf) = %s\n", buf, fmtname(buf));
      // fprintf(1, "%d\n", strcmp(fmtname(buf), "."));
      if(strcmp(de.name, ".") != 0 && strcmp(de.name, "..") != 0) {
          find(buf, file_name);
      }
    }
    break;
  }
  close(fd);
}

int main(int argc, char *argv[]) {
  if(argc != 3){
    // ls(".");
    fprintf(2, "Usage: find <path> <filename>\n");
    exit(1);
  }
  
  // fprintf(1, "begin to find %s!\n", argv[2]);
  find(argv[1], argv[2]);

  exit(0);
}