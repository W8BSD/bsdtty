#ifndef MUTEXES_H
#define MUTEXES_H

#ifdef WIN32_MUTEXES
#include <Windows.h>
typedef HANDLE mutex_t;
#endif

#ifdef POSIX_MUTEXES
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
#endif

int mutex_init(mutex_t *);
int mutex_lock(mutex_t *);
int mutex_unlock(mutex_t *);
int mutex_destroy(mutex_t *);

#endif