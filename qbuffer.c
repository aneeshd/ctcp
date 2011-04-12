#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "qbuffer.h"

void
q_init(qbuffer_t* buff, int max_size){

  assert(pthread_mutex_init( &(buff->q_mutex_) , NULL ) == 0 );
  assert(pthread_cond_init( &(buff->q_condv_pop_) , NULL ) == 0 );
  assert(pthread_cond_init( &(buff->q_condv_push_) , NULL ) == 0 );

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
  assert(pthread_mutex_lock(&buff->q_mutex_) == 0);
  
  while(buff->size == buff->max_size){
    pthread_cond_signal( &(buff->q_condv_pop_) );
    pthread_cond_wait( &(buff->q_condv_push_), &(buff->q_mutex_) );
  }
  
  buff->head++;
  buff->size++;
  buff->q_[buff->head%buff->max_size] = entry;
  
  pthread_cond_signal( &(buff->q_condv_pop_) );
  pthread_cond_signal( &(buff->q_condv_push_) );
  
  assert(pthread_mutex_unlock(&buff->q_mutex_) == 0);
}

void*
q_pop(qbuffer_t* buff){
  assert(pthread_mutex_lock(&buff->q_mutex_) == 0);

  while(buff->size == 0){
    pthread_cond_signal( &(buff->q_condv_push_) );
    pthread_cond_wait( &(buff->q_condv_pop_), &(buff->q_mutex_) );
  }
  
  void* entry = buff->q_[buff->tail%buff->max_size];

  buff->tail++;
  buff->size--;
  
  pthread_cond_signal( &(buff->q_condv_push_) );
  pthread_cond_signal( &(buff->q_condv_pop_) );

  assert(pthread_mutex_unlock(&buff->q_mutex_) == 0);
  return entry;
}

void
q_free(qbuffer_t* buff, void (*free_handler)(const void*), int begin, int n){
  assert(pthread_mutex_lock(&buff->q_mutex_) == 0);
  
  int i;
  for(i = 0; i < n; i++){
    free_handler(buff->q_[(begin+i)%buff->max_size]);
  }

  assert(pthread_mutex_unlock(&buff->q_mutex_) == 0);
}



