#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>

pthread_mutex_t table_mtx;
sem_t sup_sem;
sem_t maker_A_sem;
sem_t maker_B_sem;
sem_t maker_C_sem;

int num;
int done = 0;

void* sup(void *arg){
    for (int i = 0; i < num; i++){
        sem_wait(&sup_sem);
        pthread_mutex_lock(&table_mtx);
        char ingredients[3][30] = {"Cheese and Lettuce", "Bread and Lettuce", "Bread and Cheese"};
        if (i > 0){
            printf("\n");
        }
        int r = rand() % 3;
        if (r == 0){
            printf("Supplier places: %s\n", ingredients[0]);
            pthread_mutex_unlock(&table_mtx);
            sem_post(&maker_A_sem);
        }
        else if (r == 1){
            printf("Supplier places: %s\n", ingredients[1]);
            pthread_mutex_unlock(&table_mtx);
            sem_post(&maker_B_sem);
        }
        else{
            printf("Supplier places: %s\n", ingredients[2]);
            pthread_mutex_unlock(&table_mtx);
            sem_post(&maker_C_sem);
        }
    }

    sem_wait(&sup_sem);
    done = 1;
    sem_post(&maker_A_sem);
    sem_post(&maker_B_sem);
    sem_post(&maker_C_sem);
    return NULL;

}

void* maker_A(void *arg){
    while (1){
        sem_wait(&maker_A_sem);
        if (done) break;
        pthread_mutex_lock(&table_mtx);
        printf("Maker A picks up Cheese and Lettuce\nMaker A is making the sandwich...\nMaker A finished making the sandwich and eats it\nMaker A signals Supplier\n");
        pthread_mutex_unlock(&table_mtx);
        sem_post(&sup_sem);
    }
    return NULL;
}

void* maker_B(void *arg){
    while (1){
        sem_wait(&maker_B_sem);
        if (done) break;
        pthread_mutex_lock(&table_mtx);
        printf("Maker B picks up Bread and Lettuce\nMaker B is making the sandwich...\nMaker B finished making the sandwich and eats it\nMaker B signals Supplier\n");
        pthread_mutex_unlock(&table_mtx);
        sem_post(&sup_sem);
    }
    return NULL;
}

void* maker_C(void *arg){
    while (1){
        sem_wait(&maker_C_sem);
        if (done) break;
        pthread_mutex_lock(&table_mtx);
        printf("Maker C picks up Bread and Cheese\nMaker C is making the sandwich...\nMaker C finished making the sandwich and eats it\nMaker C signals Supplier\n");
        pthread_mutex_unlock(&table_mtx);
        sem_post(&sup_sem);
    }
    return NULL;
}

int main(){
    if (scanf("%d", &num) != 1 || num <= 0) return 0;
    srand(time(NULL));
    pthread_mutex_init(&table_mtx, NULL);
    sem_init(&sup_sem, 0, 1);
    sem_init(&maker_A_sem, 0, 0);
    sem_init(&maker_B_sem, 0, 0);
    sem_init(&maker_C_sem, 0, 0);

    pthread_t sup_id, maker_A_id, maker_B_id, maker_C_id;

    pthread_create(&sup_id, NULL, sup, NULL);
    pthread_create(&maker_A_id, NULL, maker_A, NULL);
    pthread_create(&maker_B_id, NULL, maker_B, NULL); 
    pthread_create(&maker_C_id, NULL, maker_C, NULL);

    pthread_join(sup_id, NULL);
    pthread_join(maker_A_id, NULL);
    pthread_join(maker_B_id, NULL);
    pthread_join(maker_C_id, NULL);

    pthread_mutex_destroy(&table_mtx);
    sem_destroy(&sup_sem);
    sem_destroy(&maker_A_sem);
    sem_destroy(&maker_B_sem);
    sem_destroy(&maker_C_sem);

    return 0;
}