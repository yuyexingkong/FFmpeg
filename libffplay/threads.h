/*
 * =====================================================================================
 *
 *       Filename:  threads.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  10/12/2015 11:54:57 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  DAI ZHENGHUA (), djx.zhenghua@gmail.com
 *        Company:  
 *
 * =====================================================================================
 */


#ifndef  smart_threads_INC
#define  smart_threads_INC

template<typename T>
class mutex_ptr
{
};

template<typename T>
class cond_ptr
{
};

#if defined USE_SDL_THREAD
template<>
class mutex_ptr<SDL_mutex >
{
    public:
        mutex_ptr() { mutex = SDL_CreateMutex();}
        ~mutex_ptr() { if(mutex != nullptr){SDL_DestroyMutex(mutex); mutex = NULL;} }
        void lock() { SDL_LockMutex(mutex);}
        void unlock() { SDL_UnlockMutex(mutex);}
        SDL_mutex* get() { return mutex;}
        template<typename T>
        void run(const T& func) {
            lock();
            func();
            unlock();
        }
        explicit operator bool()const  { return mutex != nullptr;}
        void destroy(){if(mutex!= nullptr){SDL_DestroyMutex(mutex); mutex= nullptr;} } 
        void create() { if(mutex== nullptr) { mutex =  SDL_CreateMutex();}}
    private:
        SDL_mutex* mutex;
};

template<>
class cond_ptr<SDL_cond>
{
    public:
        cond_ptr() { cond = SDL_CreateCond();}
        ~cond_ptr() {destroy();} 
        void wait(mutex_ptr<SDL_mutex>& mutex){ SDL_CondWait(cond, (SDL_mutex*)mutex.get());}
        void wait(mutex_ptr<SDL_mutex>& mutex, int timeout){  SDL_CondWaitTimeout(cond, mutex.get(), timeout);}
        void signal(){SDL_CondSignal(cond);}
        explicit operator bool()const  { return cond != nullptr;}
        void destroy(){if(cond != nullptr){SDL_DestroyCond(cond); cond= nullptr;} } 
        void create() { if(cond == nullptr) { cond =  SDL_CreateCond();}}
    protected:
        SDL_cond* cond;
};
typedef cond_ptr<SDL_cond> Cond;
typedef mutex_ptr<SDL_mutex> Mutex;

#include <SDL_thread.h>
class Thread
{
    public:
        Thread():m_thread(nullptr){}
        void start(int (*fn)(void *), void *arg){ m_thread= SDL_CreateThread(fn, arg);}
        void join(){ SDL_WaitThread(m_thread, NULL); m_thread = nullptr;}
        explicit operator bool()const {return m_thread != nullptr;}
    private:
        SDL_Thread* m_thread;

};
#endif

#include <mutex>
#include <condition_variable>
#include <chrono>
template<>
class mutex_ptr<std::mutex>
{

    public:
        mutex_ptr() { mutex = new std::mutex();}
        ~mutex_ptr() { if(mutex!=nullptr){ delete mutex; mutex = nullptr;} }
        void lock() { 
            mutex->lock();
        }
        void unlock() { 
            mutex->unlock();
        }
        template<typename T>
        void run(const T& func) {
            lock();
            func();
            unlock();
        }
        explicit operator bool()const  { return mutex!=nullptr;}
        std::mutex& get(){return *mutex;}
    private:
        std::mutex *mutex;
};

template<>
class cond_ptr<std::condition_variable>
{
    public:
        cond_ptr() { cond =  new std::condition_variable();}
        ~cond_ptr() {destroy();} 
        void wait(mutex_ptr<std::mutex>& mutex){ 
            std::unique_lock<std::mutex> lv(mutex.get(), std::defer_lock);
            cond->wait(lv);
        }
        void wait(mutex_ptr<std::mutex>& mutex, int timeout){  
            std::unique_lock<std::mutex> lv(mutex.get(), std::defer_lock);
            cond->wait_for(lv, std::chrono::milliseconds(timeout));
        }
        void signal(){
            cond->notify_all();
        }
        explicit operator bool()const  { return cond!=nullptr;}
        void destroy(){if(cond != nullptr){delete cond; cond= nullptr;} } 
        void create() { if(cond == nullptr) { cond =  new std::condition_variable();}}
    protected:
        std::condition_variable* cond;
};
typedef cond_ptr<std::condition_variable> Cond;
typedef mutex_ptr<std::mutex> Mutex;

#include <thread>
class Thread
{
    public:
        Thread():m_thread(nullptr){}
        void start(int (*fn)(void *), void *arg){ m_thread= new std::thread(fn, arg);}
        void join(){ if(m_thread!=nullptr){m_thread->join(); delete m_thread; m_thread = nullptr;}}
        explicit operator bool()const {return m_thread != nullptr;}
    private:
        std::thread* m_thread;
};

#endif   /* ----- #ifndef smart_threads_INC  ----- */
