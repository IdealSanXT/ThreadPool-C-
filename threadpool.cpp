#include "threadpool.h"
#include<stdio.h>
#include<malloc.h>
const int NUMBER = 2;

// @file:threadpool
// @author:IdealSanX_T
// @date:2024/6/12 9:21:15
// @brief:手撸线程池C语言版

// 任务结构体
typedef struct Task {
	void (*function)(void* arg); //函数指针void* arg为泛型参数
	void* arg; //参数地址
}Task;

// 线程池结构体
typedef struct ThreadPool {
	// 任务队列
	Task* taskQ;
	int queueCapacity;		// 容量
	int queueSize;			// 当前任务个数
	int queueFront;			// 队头 -> 取数据
	int queueRear;			// 队尾 -> 放数据

	// 定义线程的操作
	pthread_t managerID;    // 管理者线程ID
	pthread_t* threadIDs;   // 工作线程ID，工作线程有多个，定义为数组
	int minNum;             // 最小线程数量
	int maxNum;             // 最大线程数量
	int busyNum;            // 忙的线程的个数
	int liveNum;            // 存活的线程的个数
	int exitNum;            // 要销毁的线程个数，任务特别少时，需要销毁线程

	// 同步操作
	pthread_mutex_t mutexPool;  // 锁整个的线程池
	pthread_mutex_t mutexBusy;  // 锁busyNum变量
	pthread_cond_t notFull;     // 信号量，任务队列是不是满了，满了需要阻塞生产者，不能添加任务
	pthread_cond_t notEmpty;    // 信号量，任务队列是不是空了，空了需要阻塞消费者，不能消费任务

	int shutdown;				// 是不是要销毁线程池, 销毁为1, 不销毁为0
}ThreadPool;

// 初始化线程池
ThreadPool* threadPoolCreate(int min, int max, int queueSize) {
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool)); // 为线程池结构体分配内存
	if (pool == NULL) {
		printf("malloc threadpool fail...\n");
		return NULL;
	}
	do {
		// 任务队列初始化
		pool->queueCapacity = queueSize; // 任务容量
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);  // 为任务队列分配容量内存
		if (pool->taskQ == NULL) {
			printf("malloc taskQ fail...\n");
			break;
		}
		pool->queueFront = 0;
		pool->queueRear = 0;  //队头/尾指针初始化为0

		// 同步操作初始化，锁的初始化需要调用方法pthread_mutex_init()
		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0)
		{
			printf("mutex or condition init fail...\n");
			break;
		}

		// 线程的操作初始化
		pool->minNum = min;  // 线程数量最小值
		pool->maxNum = max;	 // 线程数量最大值
		pool->busyNum = 0;   // 工作线程初始化
		pool->exitNum = 0;   // 销毁消除初始化
		pool->liveNum = min; // 存活线程初始化，和最小个数相等（存活线程不等于工作线程，存活不一定工作）
		// 管理者线程ID初始化
		pthread_create(&pool->managerID, 0, manager, pool);
		// 消费者线程ID初始化，分配线程最大数容量
		pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
		if (pool->threadIDs == NULL){
			printf("malloc threadIDs fail...\n");
			break;
		}

		// 消费者线程ID数组初始化
		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);
		for (int i = 0; i < max; i++) {
			pthread_create(&pool->threadIDs[i], 0, worker, pool);
		}

		// 销毁线程池标志位初始化
		pool->shutdown = 0;

		return pool;
	} while (false);

	// 将初始化放在一次的循环里，是为了再分配内存时如果出错，使用break跳出循环，而不是return
	// 这样做是为了每次失败后，可以跳出循环来释放已经分配的内存


	// 释放资源
	if (pool && pool->threadIDs) free(pool->threadIDs);
	if (pool && pool->taskQ) free(pool->taskQ);
	if (pool) free(pool);

	return NULL;
}

// 线程池销毁
int threadPoolDestroy(ThreadPool* pool){
	if (pool == NULL){
		return -1;
	}

	// 关闭线程池
	pool->shutdown = 1;
	// 阻塞回收管理者线程
	pthread_join(pool->managerID, NULL);
	// 唤醒阻塞的消费者线程，被阻塞的消费者被唤醒后，pool->shutdown == 1时会自杀
	for (int i = 0; i < pool->liveNum; ++i){
		pthread_cond_signal(&pool->notEmpty);
	}

	// 释放堆内存
	if (pool->taskQ){
		free(pool->taskQ);
		pool->taskQ = NULL;
	}
	if (pool->threadIDs){
		free(pool->threadIDs);
	}

	pthread_mutex_destroy(&pool->mutexPool);
	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);

	free(pool);
	pool = NULL;

	return 0;
}

// 向任务队列添加任务，参数：线程池指针，函数指针，函数参数
void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg){
	// 互斥访问线程池
	pthread_mutex_lock(&pool->mutexPool);
	// 当任务队列满时并且线程池没有关闭时，阻塞此函数
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown) {
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}

	// 线程池关闭时，解锁退出
	if (pool->shutdown) {
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}

	// 添加任务
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;

	// 移动队尾指针
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;

	// 任务队列数量+1
	pool->queueSize++;

	// 释放不为空的信号量
	pthread_cond_signal(&pool->notEmpty);
	// 解锁
	pthread_mutex_unlock(&pool->mutexPool);
}

// 获取线程池中工作的线程的个数
int threadPoolBusyNum(ThreadPool* pool){

	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);
	return busyNum;
}

int threadPoolAliveNum(ThreadPool* pool){
	pthread_mutex_lock(&pool->mutexPool);
	int liveNum = pool->liveNum;
	pthread_mutex_unlock(&pool->mutexPool);
	return liveNum;
}


// 工作线程实现(消费者)
void* worker(void* arg){
	// 接收传入的线程池
	ThreadPool* pool = (ThreadPool*)arg;

	// 不断从线程池的任务队列中取出任务
	while (true) {
		// 对线程池上锁，互斥访问线程池
		pthread_mutex_lock(&pool->mutexPool);
		
		// 任务队列为空并且线程池没有关闭时
		while (pool->queueSize == 0 && !pool->shutdown) {
			// 当notEmpty信号量没有时，则为空，就阻塞工作线程，并且释放mutexPool
			// 不为空时，会被唤醒，消费notEmpty信号量，并重新对mutexPool加锁
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);


			// 判断是不是要销毁线程，exitNum会在管理者进程中需要销毁线程时赋值
			if (pool->exitNum > 0){
				pool->exitNum--;
				if (pool->liveNum > pool->minNum){
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool);
					// 自定义线程退出函数，而没有直接用pthread_exit()，
					// 需要将退出的线程所在线程队列位置进行置空
					threadExit(pool); 
				}
			}
		}

		// 判断线程池是否被关闭了
		if (pool->shutdown){
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}

		// 从任务队列中取出一个任务
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
		// 移动任务队列头结点
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--; // 任务队列中任务数量减一
		// 解锁
		pthread_cond_signal(&pool->notFull); // 消费了任务，释放不满信号量，notFull++
		pthread_mutex_unlock(&pool->mutexPool);

		printf("thread %ld start working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);  //上锁，互斥访问busyNum
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy); //解锁
		task.function(task.arg);
		free(task.arg); //释放函数参数的堆内存
		task.arg = NULL;

		printf("thread %ld end working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy); //上锁，互斥访问busyNum
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy); //解锁
	}
	return NULL;
}

// 管理者线程实现
void* manager(void* arg){
	ThreadPool* pool = (ThreadPool*)arg;
	while (!pool->shutdown)
	{
		// 每隔3s检测一次，是否需要添加/销毁线程
		Sleep(3000);

		// 取出线程池中任务的数量和当前线程的数量，互斥访问queueSize，liveNum
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		// 取出忙的线程的数量，互斥访问busyNum
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);
		

		// 添加线程，每次最多添加两个线程，也可以修改添加多个
		// 任务的个数>存活的线程个数 && 存活的线程数<最大线程数，这个条件是自定义，可以修改
		if (queueSize > liveNum && liveNum < pool->maxNum){
			// 加锁，互斥访问线程池
			pthread_mutex_lock(&pool->mutexPool);

			int counter = 0; // 添加线程的数量
			for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; ++i){
				if (pool->threadIDs[i].x == 0){ // 将任务放到线程队列中空闲位置
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);
					counter++;
					pool->liveNum++;
				}
			}
			pthread_mutex_unlock(&pool->mutexPool); // 解锁
		}

		// 销毁线程，当空闲的存活线程过多，就需要销毁一部分
		// 忙的线程*2 < 存活的线程数 && 存活的线程>最小线程数，这个条件是自定义，可以修改
		if (busyNum * 2 < liveNum && liveNum > pool->minNum){
			// 互斥访问线程池
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);

			// 让空闲工作的线程自杀
			for (int i = 0; i < NUMBER; ++i){
				// 唤醒在任务队列为空时工作的线程，使其向下执行自杀操作
				pthread_cond_signal(&pool->notEmpty);
			}
		}
	}
	return NULL;
}

void threadExit(ThreadPool* pool){
	pthread_t tid = pthread_self();
	for (int i = 0; i < pool->maxNum; ++i){
		// 寻找自杀的线程在线程队列里的位置
		if (pthread_equal(pool->threadIDs[i], tid)) {
			memset(&pool->threadIDs[i], 0, sizeof(pthread_t));
			printf("threadExit() called, %ld exiting...\n", tid);
			break;
		}
	}
	pthread_exit(NULL);
}
