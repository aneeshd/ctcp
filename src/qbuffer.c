#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <unistd.h>
#include "qbuffer.h"

/* Portable modulo operation that supports negative dividends. */
/* See http://stackoverflow.com/questions/1907565/c-python-different-behaviour-of-the-modulo-operation */
static size_t modulo(ssize_t n, size_t m) {
  /* Mod may give different result if divisor is signed. */
  ssize_t sm = (ssize_t) m;
  assert(sm > 0);
  ssize_t ret = ((n % sm) + sm) % sm;
  assert(ret >= 0);
  return (size_t) ret;
}

void
q_init(qbuffer_t* buff, int max_size){

  pthread_mutex_init( &(buff->q_mutex_) ,     NULL );
  pthread_cond_init( &(buff->q_condv_pop_) ,  NULL );
  pthread_cond_init( &(buff->q_condv_push_) , NULL );

  buff->max_size = max_size;
  buff->head = 0;
  buff->tail = 0;
  buff->size = 0;

  buff->q_ = (void**) malloc(max_size*sizeof(void*));

  int i;
  for(i = 0; i < max_size; i++){
    buff->q_[i] = NULL;
  }
}

void
q_push_back(qbuffer_t* buff, void* entry){
  pthread_mutex_lock(&buff->q_mutex_);

  while(buff->size == buff->max_size ){
    pthread_cond_signal( &(buff->q_condv_pop_) );
    pthread_cond_wait( &(buff->q_condv_push_), &(buff->q_mutex_) );
  }

  buff->head = modulo((buff->head)+1, buff->max_size);
  buff->size++;
  buff->q_[buff->head] = entry;

  pthread_cond_signal( &(buff->q_condv_pop_) );
  pthread_cond_signal( &(buff->q_condv_push_) );

  pthread_mutex_unlock(&buff->q_mutex_);
}

void
q_push_front(qbuffer_t* buff, void* entry)
{
  pthread_mutex_lock( &buff->q_mutex_ );

  while(buff->size == buff->max_size ){
    pthread_cond_signal( &buff->q_condv_pop_ );
    pthread_cond_wait( &buff->q_condv_push_, &buff->q_mutex_ );
  }

  buff->q_[buff->tail] = entry;

  buff->tail = modulo(buff->tail - 1, buff->max_size);
  buff->size++;

  pthread_cond_signal( &buff->q_condv_pop_ );
  pthread_cond_signal( &buff->q_condv_push_ );

  pthread_mutex_unlock( &buff->q_mutex_ );
}

void*
q_pop(qbuffer_t* buff){
  pthread_mutex_lock(&buff->q_mutex_);

  while(buff->size == 0){
    pthread_cond_signal( &(buff->q_condv_push_) );
    pthread_cond_wait( &(buff->q_condv_pop_), &(buff->q_mutex_) );
  }

  buff->tail = modulo( buff->tail + 1, buff->max_size );
  buff->size--;

  void* entry = buff->q_[buff->tail];

  pthread_cond_signal( &(buff->q_condv_push_) );
  pthread_cond_signal( &(buff->q_condv_pop_) );

  pthread_mutex_unlock(&buff->q_mutex_);
  return entry;
}

void
q_free(qbuffer_t* buff, void (*free_handler)(void*)){
  pthread_mutex_lock(&buff->q_mutex_);

  int i;
  for(i = buff->head; i > buff->tail; i--){
    free_handler(buff->q_[modulo(i, buff->max_size)]);
    buff->q_[modulo(i, buff->max_size)] = NULL;
  }

  buff->head = 0;
  buff->tail = 0;
  buff->size = 0;

  pthread_mutex_unlock(&buff->q_mutex_);
}



