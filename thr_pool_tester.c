#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "tprintf.h"
#include "thr_pool.h"

#define THREADS 10
#define JOBS 1000

typedef struct{
  char * msg;
 } print_job_t;

void*
print_job(void *a){
  print_job_t* job = (print_job_t*) a;
  tprintf( "Job processed by thread %lu: %s\n", pthread_self(), job->msg);
  return NULL;
}

void*
free_print_job(const void* a)
{
  print_job_t* job = (print_job_t*) a;
  free(job->msg);
  free(job);
  return NULL;
}

int
main(void){

  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  thr_pool_t pool;
  tprintf( "Initializing the threadpool\n");
  thrpool_init(&pool, THREADS);
  tprintf("Done\n");



  char* message = "Message ";
  char* important = "IMPORTANT ";

  int i = 0;
  while(i < JOBS){
    print_job_t* job = malloc(sizeof( print_job_t ));

    job->msg = malloc(40);

    char* m = (i%2) ? message : important;
    sprintf(job->msg, "%s%d", m, i);
    priority_t p = (i%2) ? LOW : HIGH;
    addJob(&pool, &print_job, job, &free_print_job, p);
    i++;
  }
  


  fprintf(stdout, "\n\n ************** KILLING THREADS ***********\n\n");

  thrpool_kill(&pool);
  return 0;
}
