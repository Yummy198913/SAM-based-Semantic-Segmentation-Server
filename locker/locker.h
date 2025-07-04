#ifndef LOCKER_H
#define LOCKER_H

#include<pthread.h>
#include<exception>
#include<semaphore.h>

class Locker{
public:
    Locker();
    ~Locker();
    bool lock();
    bool unlock();
    pthread_mutex_t *get_mutex();
private:
    pthread_mutex_t m_mutex;
};

class Condition{
public:
    Condition();
    ~Condition();
    //封装signal以及wait一些函数
    bool wait(pthread_mutex_t & mutex);
    bool timewait(pthread_mutex_t & mutex, timespec time);
    bool signal(pthread_mutex_t & mutex);
    bool broadcast(pthread_mutex_t & mutex);
private:
    pthread_cond_t m_cond;
};

class Sem{
public:
    Sem();
    Sem(int num);
    ~Sem();
    bool wait(); 
    bool post();
private:
    sem_t m_sem;
};

#endif