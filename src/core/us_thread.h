/* Understory — tiny portable threading shim (Win32 SRW locks / condition variables, or pthreads). */
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>

typedef SRWLOCK us_mutex;
typedef CONDITION_VARIABLE us_cond;
typedef HANDLE us_thread;
#define US_MUTEX_INIT SRWLOCK_INIT

static inline void us_mutex_init(us_mutex *m) { InitializeSRWLock(m); }
static inline void us_mutex_destroy(us_mutex *m) { (void)m; }
static inline void us_mutex_lock(us_mutex *m) { AcquireSRWLockExclusive(m); }
static inline void us_mutex_unlock(us_mutex *m) { ReleaseSRWLockExclusive(m); }
/* 1 when the lock was taken */
static inline int us_mutex_trylock(us_mutex *m) { return TryAcquireSRWLockExclusive(m) != 0; }
static inline void us_cond_init(us_cond *c) { InitializeConditionVariable(c); }
static inline void us_cond_destroy(us_cond *c) { (void)c; }
static inline void us_cond_wait(us_cond *c, us_mutex *m) { SleepConditionVariableSRW(c, m, INFINITE, 0); }
static inline void us_cond_signal(us_cond *c) { WakeConditionVariable(c); }

typedef struct {
	void *(*fn)(void *);
	void *arg;
} us__thread_start;

static unsigned __stdcall us__thread_tramp(void *p)
{
	us__thread_start st = *(us__thread_start *)p;
	HeapFree(GetProcessHeap(), 0, p);
	st.fn(st.arg);
	return 0;
}

/* returns 1 on success */
static inline int us_thread_create(us_thread *t, void *(*fn)(void *), void *arg)
{
	us__thread_start *st = HeapAlloc(GetProcessHeap(), 0, sizeof(*st));
	if (!st)
		return 0;
	st->fn = fn;
	st->arg = arg;
	*t = (HANDLE)_beginthreadex(NULL, 0, us__thread_tramp, st, 0, NULL);
	if (!*t) {
		HeapFree(GetProcessHeap(), 0, st);
		return 0;
	}
	return 1;
}
static inline void us_thread_join(us_thread t)
{
	WaitForSingleObject(t, INFINITE);
	CloseHandle(t);
}

#else
#include <pthread.h>

typedef pthread_mutex_t us_mutex;
typedef pthread_cond_t us_cond;
typedef pthread_t us_thread;
#define US_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER

static inline void us_mutex_init(us_mutex *m) { pthread_mutex_init(m, NULL); }
static inline void us_mutex_destroy(us_mutex *m) { pthread_mutex_destroy(m); }
static inline void us_mutex_lock(us_mutex *m) { pthread_mutex_lock(m); }
static inline void us_mutex_unlock(us_mutex *m) { pthread_mutex_unlock(m); }
static inline int us_mutex_trylock(us_mutex *m) { return pthread_mutex_trylock(m) == 0; }
static inline void us_cond_init(us_cond *c) { pthread_cond_init(c, NULL); }
static inline void us_cond_destroy(us_cond *c) { pthread_cond_destroy(c); }
static inline void us_cond_wait(us_cond *c, us_mutex *m) { pthread_cond_wait(c, m); }
static inline void us_cond_signal(us_cond *c) { pthread_cond_signal(c); }
static inline int us_thread_create(us_thread *t, void *(*fn)(void *), void *arg)
{
	return pthread_create(t, NULL, fn, arg) == 0;
}
static inline void us_thread_join(us_thread t) { pthread_join(t, NULL); }
#endif
