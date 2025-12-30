#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
int main(int argc, char *argv[]){
 int fd = open(argv[1], O_CREAT, O_EXCL);
 close(fd);
 int fd1 = open(argv[1], O_WRONLY);
 while(1){
    char *buffer;
    scanf("%s", buffer);

    if(buffer[0] == '-' && buffer[1] == '1' && buffer[2] == '\0'){
        break;
    }
    write(fd1, buffer, sizeof(buffer));
 }
    close(fd1);
}