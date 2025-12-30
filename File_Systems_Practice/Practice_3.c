#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

int main(){
    int pid, status;
    pid = fork();
    if (pid == 0){
        printf("I'm the child hehe!\n");
    }
    else if (pid > 0){
        wait(&status);
        printf("Well done kid!\n");
    }
    else{
        printf("Fork failed\n");
    }
}