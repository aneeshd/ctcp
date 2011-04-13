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

  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  thr_pool_t pool;
  tprintf( "Initializing the threadpool\n");
  thrpool_init(&pool, THREADS);
  tprintf(  "Done\n");

  print_job_t job[20];


  int i;
  for(i = 0; i < 20; i++){
    tprintf( "Adding job %d\n", i);
    job[i].msg = malloc(20);
    sprintf(job[i].msg, "Message %d", i);
    addJob(&pool, &print_job, &job[i]);
  }
  
  fprintf(stdout, "\n\n ************** KILLING THREADS ***********\n\n");
  fprintf(stdout, "Head %d, Tail %d, Size %d\n", pool.job_q.head, pool.job_q.tail, pool.job_q.size);

  // Kill the threadpool
  for(i = 0; i < THREADS; i++){
    tprintf(  "Killing thread %d\n", i);
    addJob(&pool, NULL, NULL);
    fprintf(stdout, "Head %d, Tail %d, Size %d\n", pool.job_q.head, pool.job_q.tail, pool.job_q.size);
  }
  
  thrpool_kill(&pool);
  return 0;
}
