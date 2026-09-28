#ifndef BUDO_PLATFORM_THREAD_H
#define BUDO_PLATFORM_THREAD_H

#include <stdbool.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef HANDLE BudoThread;
typedef CRITICAL_SECTION BudoMutex;
typedef DWORD(WINAPI *BudoThreadFunction)(void *);
#define BUDO_THREAD_RETURN DWORD WINAPI
#define BUDO_THREAD_RESULT 0

static inline bool budo_mutex_init(BudoMutex *mutex)
{
    return InitializeCriticalSectionEx(mutex, 0, 0) != 0;
}

static inline void budo_mutex_destroy(BudoMutex *mutex)
{
    DeleteCriticalSection(mutex);
}

static inline void budo_mutex_lock(BudoMutex *mutex)
{
    EnterCriticalSection(mutex);
}

static inline void budo_mutex_unlock(BudoMutex *mutex)
{
    LeaveCriticalSection(mutex);
}

static inline bool budo_thread_create(BudoThread *thread, BudoThreadFunction function, void *arg)
{
    *thread = CreateThread(NULL, 0, function, arg, 0, NULL);
    return *thread != NULL;
}

static inline void budo_thread_join(BudoThread thread)
{
    if (thread)
    {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
}
#else
#include <pthread.h>

typedef pthread_t BudoThread;
typedef pthread_mutex_t BudoMutex;
typedef void *(*BudoThreadFunction)(void *);
#define BUDO_THREAD_RETURN void *
#define BUDO_THREAD_RESULT NULL

static inline bool budo_mutex_init(BudoMutex *mutex)
{
    return pthread_mutex_init(mutex, NULL) == 0;
}

static inline void budo_mutex_destroy(BudoMutex *mutex)
{
    pthread_mutex_destroy(mutex);
}

static inline void budo_mutex_lock(BudoMutex *mutex)
{
    pthread_mutex_lock(mutex);
}

static inline void budo_mutex_unlock(BudoMutex *mutex)
{
    pthread_mutex_unlock(mutex);
}

static inline bool budo_thread_create(BudoThread *thread, BudoThreadFunction function, void *arg)
{
    return pthread_create(thread, NULL, function, arg) == 0;
}

static inline void budo_thread_join(BudoThread thread)
{
    pthread_join(thread, NULL);
}
#endif

#endif