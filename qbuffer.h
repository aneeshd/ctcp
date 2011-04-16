#ifndef QBUFFER_H
#define QBUFFER_H

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
  void** q_;

  int size; // The number of elements currently in the queue
} qbuffer_t;

void q_init(qbuffer_t* buff, int max_size);
void q_push_back(qbuffer_t* buff, void* entry);
void q_push_front(qbuffer_t* buff, void* entry);
void* q_pop(qbuffer_t* buff);
void q_free(qbuffer_t* buff, void*(*free_handler)(const void*));


#endif // QUEUE_H
