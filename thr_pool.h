#ifndef __THR_POOL__
#define __THR_POOL__

#include <pthread.h>

#include "qbuffer.h"

typedef enum { LOW = 0, HIGH } priority_t;

typedef struct{
  void *(*f)(void *); // function pointer for the job
  void *a; // function arguments
} job_t;

typedef struct {
  pthread_attr_t attr_;
  int nthreads_;

  qbuffer_t job_q;
  pthread_t* th_;

} thr_pool_t;

void thrpool_init(thr_pool_t* pool, int sz);
void thrpool_kill(thr_pool_t* pool);
void addJob(thr_pool_t* pool, void *(*f)(void *), void *a, priority_t p);
bool takeJob(thr_pool_t* pool, job_t** j);
void* do_worker(void *arg);

#endif // __THR_POOL__
