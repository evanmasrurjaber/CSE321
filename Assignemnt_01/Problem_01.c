#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

struct Fibonacci{
    int *arr;
    int num;
};
struct Search{
    struct Fibonacci *fib;
    int *queries;
    int size;
    int *results;
};

void* cal_fibonacci (void *arg){
    struct Fibonacci *data = (struct Fibonacci*)arg;
    int num = data->num;
    data->arr = (int*)malloc((num+1)*sizeof(int));

    if (num >= 0) data->arr[0] = 0;
    if (num >= 1) data->arr[1] = 1;
    for (int i = 2; i <= num; i++){
        data->arr[i] = data->arr[i-1] + data->arr[i-2];
    }
    pthread_exit(NULL);
}

void* search_fibonacci (void *arg){
    struct Search *data = (struct Search*)arg;
    int *fib_arr = data->fib->arr;
    int n = data->fib->num;
    for (int i=0; i < data->size; i++){
        int idx = data->queries[i];
        if (idx >= 0 && idx <= n){
            data->results[i] = fib_arr[idx];
        }
        else{
            data->results[i] = -1;
        }
    }
    pthread_exit(NULL);
}

int main(){
    int num;
    int num_searches;
    printf("Enter the term of fibonacci sequence:\n");
    if (scanf("%d", &num) != 1 || num < 0 || num > 40){
        printf("Invalid input. Please enter a number between 0 and 40.\n");
        return 0;
    }
    struct Fibonacci fib_data;
    fib_data.num = num;
    pthread_t fib_thread;
    pthread_create(&fib_thread, NULL, cal_fibonacci, &fib_data);
    pthread_join(fib_thread, NULL);

    for (int i = 0; i <= num; i++){
        printf("a[%d] = %d\n", i, fib_data.arr[i]);
    }

    printf("How many numbers you are willing to search?:\n");
    
    if (scanf("%d", &num_searches) != 1 || num_searches <= 0){
        printf("Invalid input. Please enter a positive integer.\n");
        free(fib_data.arr);
        return 0;
    }

    int *queries = (int*)malloc(num_searches * sizeof(int));
    int *results = (int*)malloc(num_searches * sizeof(int));

    for (int i = 0; i < num_searches; i++){
        printf("Enter search %d: \n", i+1);
        if (scanf("%d", &queries[i]) != 1){
            printf("Invalid input. Please enter integers only.\n");
            free(fib_data.arr);
            free(queries);
            free(results);
            return 0;
        }
    }
    struct Search search_data;
    search_data.fib = &fib_data;
    search_data.queries = queries;
    search_data.size = num_searches;
    search_data.results = results;
    pthread_t search_thread;
    pthread_create(&search_thread, NULL, search_fibonacci, &search_data);
    pthread_join(search_thread, NULL);

    for (int i = 0; i < num_searches; i++){
        printf("result of search #%d = %d\n", i+1, results[i]);
    }
    free(fib_data.arr);
    free(queries);
    free(results);
    return 0;
}

