#include "thr_pool.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include "tprintf.h"


void *
do_worker(void *arg)
{
    thr_pool_t *pool = (thr_pool_t *) arg;

    //fprintf(stdout, "*** Started thread id:  %lu***\n", pthread_self());

    while(1){
        job_t* j;

        if(!takeJob(pool, &j)){
            break; //die
        }

        (void)(j->f)(j->a); // Here is where the magic happens
        j->free_handler((void*)j->a); // Free this job arguments
        free(j);
    }

    //fprintf(stdout, "*** Dying thread id: %lu ***\n", pthread_self());
    pthread_exit(NULL);
}

void
thrpool_init(thr_pool_t *pool, int sz)
{
    pthread_attr_init(&pool->attr_);
    pthread_attr_setstacksize(&pool->attr_, 2<<20); // XXX: make sure this is fine

    pool->nthreads_ = sz;
    pool->th_       = (pthread_t*) malloc(sz*sizeof(pthread_t));
    q_init(&pool->job_q, 2*sz);

    pthread_t t[sz];

    int i;
    for(i = 0; i < sz; i++){
        pthread_create(&t[i], &pool->attr_, &do_worker, pool);
        pool->th_[i] = t[i];
    }
}

void
thrpool_kill(thr_pool_t* pool)
{
    // Kill the threadpool
    int i;
    for(i = 0; i < pool->nthreads_; i++){
        addJob(pool, NULL, NULL, NULL, LOW);
    }

    for(i = 0; i < pool->nthreads_; i++){
        pthread_join(pool->th_[i], NULL);
    }
    pthread_attr_destroy(&pool->attr_);
}

void
addJob(thr_pool_t* pool, void *(*f)(void *), void *a, void (*free_handler)(void *), priority_t p)
{
    job_t* j = (job_t*) malloc(sizeof(job_t));
    j->f = f;
    j->a = a;
    j->free_handler = free_handler;

    if(HIGH == p){
        q_push_front(&pool->job_q, j);
    }else if(LOW == p){
        q_push_back(&pool->job_q, j);
    }
}

bool
takeJob(thr_pool_t* pool, job_t** j)
{
    *j = (job_t*) q_pop(&pool->job_q);
    return ((*j)->f != NULL);
}

