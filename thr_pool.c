#include "thr_pool.h"
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

void
do_worker(void *arg)
{
  thr_pool_t *pool (thr_pool_t *) arg;
  while(1){
    job_t j;
    if(!takeJob(pool, &j)){
      break; //die

      (void)(j.f)(j.a);
    }
  }
  pthread_exit(NULL);
}

void
thrpool_init(thr_pool_t *pool, int sz)
{
  pthread_attr_init(&pool->attr_);
  pthread_attr_setstacksize(&pool->attr_, 128<<10);

  pool->nthreads_ = sz;
  pool->th_ = malloc(sz*sizeof(pthread_t));

  int i;
  for(i = 0; i < sz; i++){
    pthread_t t;
    assert(pthread_create(&t, &pool->attr_, &do_worker, pool) == 0);
    th_[i] = t;
  }
}

void 
thrpool_kill(thr_pool_t* pool)
{
  int i;
  for(i = 0; i < pool->nthreads_, i++){
    assert(pthread_join(pool->th_[i], NULL) == 0);
  }
  assert(pthread_attr_destroy(pool->attr_) == 0);
}

void
addJob(thr_pool_t* pool, void *(*f)(void *), void *a);
{
  job_t j;
  j.f = f;
  j.a = a;
  q_push(pool->job_q, &j);
}

bool
takejob(thr_pool_t* pool, job_t* j);
{
  job_t* j;
  j = (job_t*) q_pop(pool->job_q);
  return (j->f != NULL);
}

