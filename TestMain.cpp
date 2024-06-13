#include<stdio.h>
#include<string>
#include"threadpool.h"

// @file:TestMain
// @author:IdealSanX_T
// @date:2024/6/12 19:27:58
// @brief:

void taskFunc(void* arg){
    int num = *(int*)arg;
    printf("thread %ld is working, number = %d\n", pthread_self(), num);
    Sleep(1000);
}

int main() {
    // 创建线程池
    ThreadPool* pool = threadPoolCreate(3, 10, 100);
    
    for (int i = 0; i < 10; ++i){
        int* num = (int*)malloc(sizeof(int));
        *num = i;
        printf("%d\n", *num);
        threadPoolAdd(pool, taskFunc, num);
    }

    Sleep(3000);

    threadPoolDestroy(pool);
    return 0;
}