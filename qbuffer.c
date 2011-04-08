#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "qbuffer.h"

void
q_init(qbuffer_t* buff){

  assert(pthread_mutex_init( &(buff->q_mutex_) , NULL ) == 0 );
  assert(pthread_cond_init( &(buff->q_condv_pop_) , NULL ) == 0 );
  assert(pthread_cond_init( &(buff->q_condv_push_) , NULL ) == 0 );

  buff->head = 0;
  buff->tail = 0;
  buff->size = 0;

  int i;
  for(i = 0; i < MAX_Q_SIZE; i++){
    buff->q_[i] = NULL;
  }
}

void
q_push(qbuffer_t* buff, Data_Pckt* packet){
  assert(pthread_mutex_lock(&buff->q_mutex_) == 0);
  
  while(buff->size == MAX_Q_SIZE){
    pthread_cond_signal( &(buff->q_condv_pop_) );
    pthread_cond_wait( &(buff->q_condv_push_), &(buff->q_mutex_) );
  }
  
  buff->head++;
  buff->size++;
  buff->q_[buff->head%MAX_Q_SIZE] = packet;
  
  pthread_cond_signal( &(buff->q_condv_pop_) );
  pthread_cond_signal( &(buff->q_condv_push_) );
  
  assert(pthread_mutex_unlock(&buff->q_mutex_) == 0);
}

Data_Pckt*
q_pop(qbuffer_t* buff){
  assert(pthread_mutex_lock(&buff->q_mutex_) == 0);

  while(buff->size == 0){
    pthread_cond_signal( &(buff->q_condv_push_) );
    pthread_cond_wait( &(buff->q_condv_pop_), &(buff->q_mutex_) );
  }
  
  Data_Pckt* packet = buff->q_[buff->tail%MAX_Q_SIZE];

  buff->tail++;
  buff->size--;
  
  pthread_cond_signal( &(buff->q_condv_push_) );
  pthread_cond_signal( &(buff->q_condv_pop_) );

  assert(pthread_mutex_unlock(&buff->q_mutex_) == 0);
  return packet;
}


void
q_free(qbuffer_t* buff, int begin, int n){
  assert(pthread_mutex_lock(&buff->q_mutex_) == 0);
  
  int i;
  for(i = 0; i < n; i++){
    free(buff->q_[(begin+i)%MAX_Q_SIZE]->packet_coeff);
    free(buff->q_[(begin+i)%MAX_Q_SIZE]->payload);
    free(buff->q_[(begin+i)%MAX_Q_SIZE]);
  }

  assert(pthread_mutex_unlock(&buff->q_mutex_) == 0);
}
