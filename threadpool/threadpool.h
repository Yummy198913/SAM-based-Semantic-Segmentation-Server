#ifndef THREADPOOL_H
#define THREADPOOL_H

#include"../locker/locker.h"
#include<cstdio>
#include<list>

//线程池类，定义为模板类是为了代码复用,T是任务类
template<typename T>
class Threadpool{

public:
    Threadpool(int thread_num=8, int max_requests=10000);

    ~Threadpool();

    bool append(T * request);

private:
    //子线程工作函数
    static void * work(void * arg);
    //工作线程执行的函数
    void run();
    
private:
    //线程数量
    int m_thread_num;

    //线程池数组
    pthread_t * m_pthread_list;

    //请求队列中最多允许的请求处理的数量
    int m_max_requests;

    //请求队列
    std::list<T *> m_workqueue;

    //互斥锁,因为工作队列共享
    Locker m_queuelocker;

    //信号量,判断是否有任务需要处理
    Sem m_queuesem;

    //是否结束线程
    bool m_stop;

};

template< typename T>
Threadpool<T>::Threadpool(int thread_num, int max_requests):
    m_thread_num(thread_num),
    m_max_requests(max_requests),
    m_stop(false),
    //初始化m_thread_lsit
    m_pthread_list(NULL) {
    if ((m_thread_num <= 0) || (m_max_requests <= 0))
    {
        throw std::exception();
    }

    //创建线程数组
    m_pthread_list = new pthread_t[m_thread_num];
    if (!m_pthread_list) // 创建失败
    {
        throw std::exception();
    }

    //创建m_thread_num个线程，并设置为线程脱离（以后自己释放数组）
    for (int i =0; i < m_thread_num; i++)
    {
        printf("Create the %dth thread\n", i);
        int ret = pthread_create(&m_pthread_list[i], NULL, work, this);
        if (ret != 0)
        {
            //出错，释放数组
            delete[] m_pthread_list;
            throw std::exception();
        }
        
        ret = pthread_detach(m_pthread_list[i]);
        if (ret != 0)
        {
            //分离失败
            delete [] m_pthread_list;
            throw std::exception();
        }
    }
};

template<typename T>
Threadpool<T>::~Threadpool(){
    //释放线程数组
    delete[] m_pthread_list;
    m_stop = true;
};

template<typename T>
bool Threadpool<T>::append(T * request){
    // 先判断能不能添加
    //加锁
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    //可以添加
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuesem.post();

    return true;
};

template<typename T>
void * Threadpool<T>::work(void * arg)
{
    //this传递到了函数当中
        Threadpool * pool = (Threadpool * ) arg;
        pool->run();
        return pool;
};

template<typename T>
void Threadpool<T>::run()
{
    while(!m_stop)
        {
            m_queuesem.wait();
            m_queuelocker.lock();
            if (m_workqueue.empty())
            {
                m_queuelocker.unlock();
                continue;
            }
            //获取请求
            T * request = m_workqueue.front();
            m_workqueue.pop_front();
            m_queuelocker.unlock();
            
            if (! request)
            {
                continue;
            }
            printf("进入run工作函数\n");
            request->process(); // 线程工作函数
        }
};

#endif