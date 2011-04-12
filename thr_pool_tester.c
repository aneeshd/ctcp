#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "tprintf.h"
#include "thr_pool.h"

#define THREADS 5

typedef struct{
  char * msg;
} print_job_t;

void*
print_job(void *a){
  print_job_t* job = (print_job_t*) a;
  tprintf( "%s\n", job->msg);
  return NULL;
}


int
main(void){
  thr_pool_t pool;
  tprintf( "Initializing the threadpool\n");
  thrpool_init(&pool, THREADS);
  tprintf(  "Done\n");


  int i;
  for(i = 0; i < 20; i++){
    tprintf( "Adding job %d\n", i);
    print_job_t job;
    job.msg = malloc(20);
    sprintf(job.msg, "Message %d", i);
    addJob(&pool, &print_job, &job);
  }
  
  // Kill the threadpool
  /*  for(i = 0; i < THREADS; i++){
    tprintf(  "Killing thread %d\n", i);
    addJob(&pool, NULL, NULL);
    }*/
  
  //  thrpool_kill(&pool);
  return 0;
}
