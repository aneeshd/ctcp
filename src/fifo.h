#ifndef FIFO_H
#define FIFO_H

#include <pthread.h>
#include "util.h"

/*
 * The following is an implementation of a thread-safe
 * circular buffer for use in conjunction with the CTCP
 * implementation.
 */

typedef struct{
  pthread_mutex_t q_mutex_;
  pthread_cond_t q_condv_pop_;
  pthread_cond_t q_condv_push_;

  int max_size; // Maximum size of the buffer
  int head; // Index of the first element in the queue
  int tail; // Index of the last element in the queue
  char* q_;

  int size; // The number of elements currently in the queue
} fifo_t;

void fifo_init(fifo_t* Q, int max_size);
size_t fifo_getspace(fifo_t* Q);
size_t fifo_push(fifo_t* Q, const void *buf, size_t n);
size_t fifo_pop(fifo_t* Q, void *buf, size_t n);
void fifo_release(fifo_t* Q);
void fifo_free(fifo_t* Q);


#endif // FIFO_H
