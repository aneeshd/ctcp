#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "qbuffer.h"

void
q_init(qbuffer_t* buff, int max_size){

  pthread_mutex_init( &(buff->q_mutex_) , NULL );
  pthread_cond_init( &(buff->q_condv_pop_) ,NULL );
  pthread_cond_init( &(buff->q_condv_push_) , NULL );

  buff->max_size = max_size;
  buff->head = 0;
  buff->tail = 0;
  buff->size = 0;

  buff->q_ = malloc(max_size*sizeof(void*));

  int i;
  for(i = 0; i < max_size; i++){
    buff->q_[i] = NULL;
  }
}

void
q_push(qbuffer_t* buff, void* entry){
  pthread_mutex_lock(&buff->q_mutex_);
  
  while(buff->size == buff->max_size){
    pthread_cond_signal( &(buff->q_condv_pop_) );
    pthread_cond_wait( &(buff->q_condv_push_), &(buff->q_mutex_) );
  }
  
  buff->head++;
  buff->size++;
  buff->q_[buff->head%buff->max_size] = entry;
  
  pthread_cond_signal( &(buff->q_condv_pop_) );
  pthread_cond_signal( &(buff->q_condv_push_) );
  
  pthread_mutex_unlock(&buff->q_mutex_);
}

void*
q_pop(qbuffer_t* buff){
  pthread_mutex_lock(&buff->q_mutex_);

  while(buff->size == 0){
    pthread_cond_signal( &(buff->q_condv_push_) );
    pthread_cond_wait( &(buff->q_condv_pop_), &(buff->q_mutex_) );
  }
  
  buff->tail++;
  buff->size--;
  
  void* entry = buff->q_[buff->tail%buff->max_size];

  pthread_cond_signal( &(buff->q_condv_push_) );
  pthread_cond_signal( &(buff->q_condv_pop_) );

  pthread_mutex_unlock(&buff->q_mutex_);
  return entry;
}

void
q_free(qbuffer_t* buff, void (*free_handler)(const void*), int begin, int n){
  pthread_mutex_lock(&buff->q_mutex_);
  
  int i;
  for(i = 0; i < n; i++){
    free_handler(buff->q_[(begin+i)%buff->max_size]);
  }

  pthread_mutex_unlock(&buff->q_mutex_);
}



