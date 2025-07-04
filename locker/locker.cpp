/*
线程同步机制封装类
*/
#include"locker.h"

// 互斥锁类

Locker::Locker()
{
    if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            // 异常
            throw std::exception();
        }
}

Locker::~Locker(){
    pthread_mutex_destroy(&m_mutex);
}

bool Locker::lock()
{
    return pthread_mutex_lock(&m_mutex) == 0;
}

bool Locker::unlock()
{
    return pthread_mutex_unlock(&m_mutex) == 0;
}

pthread_mutex_t *Locker::get_mutex()
{
    return &m_mutex;
}

Condition::Condition()
{
    if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            throw std::exception();
        }
}

Condition::~Condition()
{
    pthread_cond_destroy(&m_cond);
}

bool Condition::wait(pthread_mutex_t & mutex)
{
    return pthread_cond_wait(&m_cond, &mutex) == 0;
}

bool Condition::timewait(pthread_mutex_t & mutex, timespec time)
{
    return pthread_cond_timedwait(&m_cond, &mutex, &time) == 0;
}

bool Condition::signal(pthread_mutex_t & mutex)
{
    return pthread_cond_signal(&m_cond) == 0;
}

bool Condition::broadcast(pthread_mutex_t & mutex)
{
    return pthread_cond_broadcast(&m_cond) == 0;
}

Sem::Sem()
{
    if( sem_init( &m_sem, 0, 0 ) != 0 ) {
        throw std::exception();
    }
}


Sem::Sem(int num)
{
    if (sem_init(&m_sem, 0, num) != 0)
    {
        throw std::exception();
    }
}

Sem::~Sem()
{
    sem_destroy(&m_sem);
}

//wait
bool Sem::wait()
{
    return sem_wait(&m_sem) == 0;
}

//post
bool Sem::post()
{
    return sem_post(&m_sem) == 0;
}