#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <unistd.h>
#include "fifo.h"

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
fifo_init(fifo_t* Q, int max_size){

  pthread_mutex_init( &(Q->q_mutex_) ,     NULL );
  pthread_cond_init( &(Q->q_condv_pop_) ,  NULL );
  pthread_cond_init( &(Q->q_condv_push_) , NULL );

  Q->max_size = max_size;
  Q->head = 0;
  Q->tail = 0;
  Q->size = 0;

  Q->q_ = (char*) calloc(max_size, sizeof(char));
}


size_t
fifo_push(fifo_t* Q, const void *buf, size_t n){
  pthread_mutex_lock(&Q->q_mutex_);

  while(Q->size == Q->max_size){
    pthread_cond_signal( &(Q->q_condv_pop_) );
    pthread_cond_wait( &(Q->q_condv_push_), &(Q->q_mutex_) );
  }

  size_t push_size = MIN(n, Q->max_size - Q->size);

  if(push_size <= Q->max_size - Q->tail){

    memcpy(Q->q_ + Q->tail, buf, push_size);

  }else{

    size_t res = fifo_push(Q, buf, Q->max_size - Q->tail);
    res        = fifo_push(Q, buf+res, push_size - res);
  }

  Q->tail = modulo(Q->tail + push_size, Q->max_size);
  Q->size += push_size;

  pthread_cond_signal( &(Q->q_condv_pop_) );
  pthread_cond_signal( &(Q->q_condv_push_) );

  pthread_mutex_unlock(&Q->q_mutex_);

  return push_size;
}


size_t fifo_pop(fifo_t* Q, void *buf, size_t n){
  pthread_mutex_lock(&Q->q_mutex_);

  while(Q->size == 0){
    pthread_cond_signal( &(Q->q_condv_push_) );
    pthread_cond_wait( &(Q->q_condv_pop_), &(Q->q_mutex_) );
  }

  size_t pop_size = MIN(n, Q->size);

  if (Q->head + pop_size <= Q->max_size){

    memcpy(buf, Q->q_ + Q->head, pop_size);

  }else{
    
    size_t res = fifo_pop(Q, buf, Q->max_size - Q->head);
    res        = fifo_pop(Q, buf+res, pop_size - res);

  }


  Q->head = modulo(Q->head + pop_size, Q->max_size);
  Q->size -= pop_size;

  pthread_cond_signal( &(Q->q_condv_push_) );
  pthread_cond_signal( &(Q->q_condv_pop_) );

  pthread_mutex_unlock(&Q->q_mutex_);

  return pop_size;
}

void
fifo_free(fifo_t *Q){
  pthread_mutex_lock(&Q->q_mutex_);

  Q->head = 0;
  Q->tail = 0;
  Q->size = 0;

  free(Q->q_);

  pthread_mutex_unlock(&Q->q_mutex_);
}



