#include "threadpool.h"
#include<stdio.h>
#include<malloc.h>
const int NUMBER = 2;

// @file:threadpool
// @author:IdealSanX_T
// @date:2024/6/12 9:21:15
// @brief:��ߣ�̳߳�C���԰�

// ����ṹ��
typedef struct Task {
	void (*function)(void* arg); //����ָ��void* argΪ���Ͳ���
	void* arg; //������ַ
}Task;

// �̳߳ؽṹ��
typedef struct ThreadPool {
	// �������
	Task* taskQ;
	int queueCapacity;		// ����
	int queueSize;			// ��ǰ�������
	int queueFront;			// ��ͷ -> ȡ����
	int queueRear;			// ��β -> ������

	// �����̵߳Ĳ���
	pthread_t managerID;    // �������߳�ID
	pthread_t* threadIDs;   // �����߳�ID�������߳��ж��������Ϊ����
	int minNum;             // ��С�߳�����
	int maxNum;             // ����߳�����
	int busyNum;            // æ���̵߳ĸ���
	int liveNum;            // �����̵߳ĸ���
	int exitNum;            // Ҫ���ٵ��̸߳����������ر���ʱ����Ҫ�����߳�

	// ͬ������
	pthread_mutex_t mutexPool;  // ���������̳߳�
	pthread_mutex_t mutexBusy;  // ��busyNum����
	pthread_cond_t notFull;     // �ź�������������ǲ������ˣ�������Ҫ���������ߣ������������
	pthread_cond_t notEmpty;    // �ź�������������ǲ��ǿ��ˣ�������Ҫ���������ߣ�������������

	int shutdown;				// �ǲ���Ҫ�����̳߳�, ����Ϊ1, ������Ϊ0
}ThreadPool;

// ��ʼ���̳߳�
ThreadPool* threadPoolCreate(int min, int max, int queueSize) {
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool)); // Ϊ�̳߳ؽṹ������ڴ�
	if (pool == NULL) {
		printf("malloc threadpool fail...\n");
		return NULL;
	}
	do {
		// ������г�ʼ��
		pool->queueCapacity = queueSize; // ��������
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);  // Ϊ������з��������ڴ�
		if (pool->taskQ == NULL) {
			printf("malloc taskQ fail...\n");
			break;
		}
		pool->queueFront = 0;
		pool->queueRear = 0;  //��ͷ/βָ���ʼ��Ϊ0

		// ͬ��������ʼ�������ĳ�ʼ����Ҫ���÷���pthread_mutex_init()
		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0)
		{
			printf("mutex or condition init fail...\n");
			break;
		}

		// �̵߳Ĳ�����ʼ��
		pool->minNum = min;  // �߳�������Сֵ
		pool->maxNum = max;	 // �߳��������ֵ
		pool->busyNum = 0;   // �����̳߳�ʼ��
		pool->exitNum = 0;   // ����������ʼ��
		pool->liveNum = min; // ����̳߳�ʼ��������С������ȣ�����̲߳����ڹ����̣߳���һ��������
		// �������߳�ID��ʼ��
		pthread_create(&pool->managerID, 0, manager, pool);
		// �������߳�ID��ʼ���������߳����������
		pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
		if (pool->threadIDs == NULL){
			printf("malloc threadIDs fail...\n");
			break;
		}

		// �������߳�ID�����ʼ��
		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);
		for (int i = 0; i < max; i++) {
			pthread_create(&pool->threadIDs[i], 0, worker, pool);
		}

		// �����̳߳ر�־λ��ʼ��
		pool->shutdown = 0;

		return pool;
	} while (false);

	// ����ʼ������һ�ε�ѭ�����Ϊ���ٷ����ڴ�ʱ�������ʹ��break����ѭ����������return
	// ��������Ϊ��ÿ��ʧ�ܺ󣬿�������ѭ�����ͷ��Ѿ�������ڴ�


	// �ͷ���Դ
	if (pool && pool->threadIDs) free(pool->threadIDs);
	if (pool && pool->taskQ) free(pool->taskQ);
	if (pool) free(pool);

	return NULL;
}

// �̳߳�����
int threadPoolDestroy(ThreadPool* pool){
	if (pool == NULL){
		return -1;
	}

	// �ر��̳߳�
	pool->shutdown = 1;
	// �������չ������߳�
	pthread_join(pool->managerID, NULL);
	// �����������������̣߳��������������߱����Ѻ�pool->shutdown == 1ʱ����ɱ
	for (int i = 0; i < pool->liveNum; ++i){
		pthread_cond_signal(&pool->notEmpty);
	}

	// �ͷŶ��ڴ�
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

// ���������������񣬲������̳߳�ָ�룬����ָ�룬��������
void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg){
	// ��������̳߳�
	pthread_mutex_lock(&pool->mutexPool);
	// �����������ʱ�����̳߳�û�йر�ʱ�������˺���
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown) {
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}

	// �̳߳عر�ʱ�������˳�
	if (pool->shutdown) {
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}

	// �������
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;

	// �ƶ���βָ��
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;

	// �����������+1
	pool->queueSize++;

	// �ͷŲ�Ϊ�յ��ź���
	pthread_cond_signal(&pool->notEmpty);
	// ����
	pthread_mutex_unlock(&pool->mutexPool);
}

// ��ȡ�̳߳��й������̵߳ĸ���
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


// �����߳�ʵ��(������)
void* worker(void* arg){
	// ���մ�����̳߳�
	ThreadPool* pool = (ThreadPool*)arg;

	// ���ϴ��̳߳ص����������ȡ������
	while (true) {
		// ���̳߳���������������̳߳�
		pthread_mutex_lock(&pool->mutexPool);
		
		// �������Ϊ�ղ����̳߳�û�йر�ʱ
		while (pool->queueSize == 0 && !pool->shutdown) {
			// ��notEmpty�ź���û��ʱ����Ϊ�գ������������̣߳������ͷ�mutexPool
			// ��Ϊ��ʱ���ᱻ���ѣ�����notEmpty�ź����������¶�mutexPool����
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);


			// �ж��ǲ���Ҫ�����̣߳�exitNum���ڹ����߽�������Ҫ�����߳�ʱ��ֵ
			if (pool->exitNum > 0){
				pool->exitNum--;
				if (pool->liveNum > pool->minNum){
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool);
					// �Զ����߳��˳���������û��ֱ����pthread_exit()��
					// ��Ҫ���˳����߳������̶߳���λ�ý����ÿ�
					threadExit(pool); 
				}
			}
		}

		// �ж��̳߳��Ƿ񱻹ر���
		if (pool->shutdown){
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}

		// �����������ȡ��һ������
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
		// �ƶ��������ͷ���
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--; // �������������������һ
		// ����
		pthread_cond_signal(&pool->notFull); // �����������ͷŲ����ź�����notFull++
		pthread_mutex_unlock(&pool->mutexPool);

		printf("thread %ld start working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);  //�������������busyNum
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy); //����
		task.function(task.arg);
		free(task.arg); //�ͷź��������Ķ��ڴ�
		task.arg = NULL;

		printf("thread %ld end working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy); //�������������busyNum
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy); //����
	}
	return NULL;
}

// �������߳�ʵ��
void* manager(void* arg){
	ThreadPool* pool = (ThreadPool*)arg;
	while (!pool->shutdown)
	{
		// ÿ��3s���һ�Σ��Ƿ���Ҫ���/�����߳�
		Sleep(3000);

		// ȡ���̳߳�������������͵�ǰ�̵߳��������������queueSize��liveNum
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		// ȡ��æ���̵߳��������������busyNum
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);
		

		// ����̣߳�ÿ�������������̣߳�Ҳ�����޸���Ӷ��
		// ����ĸ���>�����̸߳��� && �����߳���<����߳���������������Զ��壬�����޸�
		if (queueSize > liveNum && liveNum < pool->maxNum){
			// ��������������̳߳�
			pthread_mutex_lock(&pool->mutexPool);

			int counter = 0; // ����̵߳�����
			for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; ++i){
				if (pool->threadIDs[i].x == 0){ // ������ŵ��̶߳����п���λ��
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);
					counter++;
					pool->liveNum++;
				}
			}
			pthread_mutex_unlock(&pool->mutexPool); // ����
		}

		// �����̣߳������еĴ���̹߳��࣬����Ҫ����һ����
		// æ���߳�*2 < �����߳��� && �����߳�>��С�߳���������������Զ��壬�����޸�
		if (busyNum * 2 < liveNum && liveNum > pool->minNum){
			// ��������̳߳�
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);

			// �ÿ��й������߳���ɱ
			for (int i = 0; i < NUMBER; ++i){
				// �������������Ϊ��ʱ�������̣߳�ʹ������ִ����ɱ����
				pthread_cond_signal(&pool->notEmpty);
			}
		}
	}
	return NULL;
}

void threadExit(ThreadPool* pool){
	pthread_t tid = pthread_self();
	for (int i = 0; i < pool->maxNum; ++i){
		// Ѱ����ɱ���߳����̶߳������λ��
		if (pthread_equal(pool->threadIDs[i], tid)) {
			memset(&pool->threadIDs[i], 0, sizeof(pthread_t));
			printf("threadExit() called, %ld exiting...\n", tid);
			break;
		}
	}
	pthread_exit(NULL);
}
