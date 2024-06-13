#pragma once
#include<pthread.h>
#include<windows.h>
typedef struct ThreadPool ThreadPool;
// �����̳߳ز���ʼ�������������С�̸߳������Լ�����������С
ThreadPool* threadPoolCreate(int min, int max, int queueSize);
// �����̳߳�
int threadPoolDestroy(ThreadPool* pool);

// ���̳߳��������
void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg);

// ��ȡ�̳߳��й������̵߳ĸ���
int threadPoolBusyNum(ThreadPool* pool);

// ��ȡ�̳߳��л��ŵ��̵߳ĸ���
int threadPoolAliveNum(ThreadPool* pool);



//////////////////////
// 
// �������߳�(�������߳�)����������Ҫ���ϵش��̳߳ص��������ȥ������
void* worker(void* arg);
// �������߳�������
void* manager(void* arg);
// �����߳��˳�
void threadExit(ThreadPool* pool);
