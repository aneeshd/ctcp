#include "thr_pool.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include "tprintf.h"

void *
do_worker(void *arg)
{
  thr_pool_t *pool = (thr_pool_t *) arg;
  while(1){
    job_t j;
    tprintf("Taking job\n");
    if(!takeJob(pool, &j)){
      break; //die
    }
    
    tprintf("Got it\n");

    //(void)(j.f)(j.a);
  }
  pthread_exit(NULL);
}

void
thrpool_init(thr_pool_t *pool, int sz)
{
  pthread_attr_init(&pool->attr_);
  pthread_attr_setstacksize(&pool->attr_, 2<<20); // XXX: make sure this is fine

  pool->nthreads_ = sz;
  pool->th_ = malloc(sz*sizeof(pthread_t));
  q_init(&pool->job_q, 2*sz);

  int i;
  for(i = 0; i < sz; i++){
    pthread_t t;
    pthread_create(&t, &pool->attr_, &do_worker, pool);
    pool->th_[i] = t;
  }
}

void 
thrpool_kill(thr_pool_t* pool)
{
  int i;
  for(i = 0; i < pool->nthreads_; i++){
   pthread_join(pool->th_[i], NULL);
  }
 pthread_attr_destroy(&pool->attr_);
}

void
addJob(thr_pool_t* pool, void *(*f)(void *), void *a)
{
  job_t j;
  j.f = f;
  j.a = a;
  q_push(&pool->job_q, &j);
}

bool
takeJob(thr_pool_t* pool, job_t* j)
{
  j = (job_t*) q_pop(&pool->job_q);
  return (j->f != NULL);
}

