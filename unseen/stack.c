/*
 * data stack for use with automaton infrastructure
 */

#include "stack.h"
#include "mem.h"
#include "code.h"

#define DEFAULT_STACK_SIZE 256

struct stack {
   int size;
   DataStackEntry *theStack;
   DataStackEntry *sp;
   DataStackEntry *limit;
};

Stack *stack_create(int size) {
   int N = (size > 0) ? size : DEFAULT_STACK_SIZE;
   Stack *st = (Stack *)mem_alloc(sizeof(Stack));
   if (st) {
      st->theStack = (DataStackEntry *)mem_alloc(N*sizeof(DataStackEntry));
      if (! st->theStack) {
         mem_free(st);
         st = NULL;
      } else {
         st->size = N;
         st->sp = st->theStack;
         st->limit = st->sp + N;
      }
   }
   return st;
}

void stack_destroy(Stack *st) {
   mem_free(st->theStack);
   mem_free(st);
}

void reset(Stack *st) {
   st->sp = st->theStack;
}

void push(Stack *st, DataStackEntry d) {
   if (st->sp >= st->limit)
      execerror("stack overflow", NULL);
   *(st->sp++) = d;
}

DataStackEntry pop(Stack *st) {
   if (st->sp <= st->theStack)
      execerror("stack underflow", NULL);
   return *(--st->sp);
}
