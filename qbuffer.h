#ifndef QBUFFER_H
#define QBUFFER_H

#include <pthread.h>
#include "util.h"

/*
 * The following is an implementation of a thread-safe
 * circular buffer for use in conjunction with the CTCP
 * implementation.
 */

#define MAX_Q_SIZE 2*BLOCK_SIZE*sizeof(Data_Pckt*)

typedef struct{
  pthread_mutex_t q_mutex_;
  pthread_cond_t q_condv_pop_;
  pthread_cond_t q_condv_push_;

  int head; // Index of the first element in the queue
  int tail; // Index of the last element in the queue
  Data_Pckt* q_[MAX_Q_SIZE];

  int size; // The number of elements currently in the queue
} qbuffer_t;

void q_init(qbuffer_t* buff);
void q_push(qbuffer_t* buff, Data_Pckt* packet);
Data_Pckt* q_pop(qbuffer_t* buff);
void q_free(qbuffer_t* buff, int begin, int n);


#endif // QUEUE_H
