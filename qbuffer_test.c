/*
 * Qbuffer tester
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include "qbuffer.h"
#include "util.h"

int nt = 10;
qbuffer_t q;

pthread_mutex_t check_mutex;

int
main(int argc, char *argv[])
{
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  srandom(getpid());

  

}


