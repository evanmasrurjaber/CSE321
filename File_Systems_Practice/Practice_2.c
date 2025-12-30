#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
int main(){
    pid_t pid;
    pid = fork();
    if (pid == 0){
        printf("\n I'm the child Process");
    }
    else if (pid > 0){
        printf("\n I'm the Parent process. My child process is %d", pid);
    }
    else{
        perror("error in fork");
    }
}