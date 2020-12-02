/*
 * Automaton ADT implementation
 */

#include "automaton.h"
#include "topic.h"
#include "linkedlist.h"
#include "tshtable.h"
#include "machineContext.h"
#include "code.h"
#include "mem.h"
#include "stack.h"
#include "timestamp.h"
#include "dsemem.h"
#include "topic.h"
#include "a_globals.h"
#include <stdio.h>
#include <pthread.h>
#include <setjmp.h>
#include <string.h>
#include <time.h>
#include "srpc.h"

#define DEFAULT_HASH_TABLE_SIZE 20

struct automaton {
   short must_exit;
   short has_exited;
   unsigned long id;
   pthread_mutex_t lock;
   pthread_cond_t cond;
   LinkedList *events;
   TSHTable *topics;
   TSHTable *variables;
   InstructionEntry *init;
   InstructionEntry *behav;
   RpcConnection rpc;
};

static TSHTable *automatons = NULL;
static struct timespec delay = { 1, 0};
static pthread_mutex_t compile_lock = PTHREAD_MUTEX_INITIALIZER;

extern int a_parse(void);
extern void ap_init();
extern void a_init();

/*
 * keys used for storing jmp_buf and execution error information with thread
 */

pthread_key_t jmpbuf_key;
pthread_key_t execerr_key;

static void *thread_func(void *args) {
   Event *current;
   Automaton *self = (Automaton *)args;
   DataStackEntry *dse;
   MachineContext mc;
   char **keys;
   int i, n;
   jmp_buf begin;
   int execerr = 0;
   int ifbehav = 0;
   char buf[20];
   void *dummy;

   pthread_mutex_lock(&(self->lock));
   mc.variables = self->variables;
   mc.stack = stack_create(0);
   mc.currentTopic = NULL;
   mc.au = self;
   reset(mc.stack);
   if (self->init) {
      if (! setjmp(begin)) {
         (void)pthread_setspecific(jmpbuf_key, (void *)begin);
         execute(&mc, self->init);
         execerr = 0;
         ifbehav = 1;
      } else
         execerr = 1;
   }
   while (!execerr && !self->must_exit) {
      DataStackEntry d;
      char *name;
      while (!self->must_exit && ll_length(self->events) == 0) {
         pthread_cond_wait(&(self->cond), &(self->lock));
      }
      if (self->must_exit)
         break;
      current = (Event *)ll_remove(self->events);
      name = tsht_lookup(self->topics, ev_topic(current));
      if (! name) {
         continue;
      }
      d.type = dEVENT;
      d.value.ev_v = current;
      (void)tsht_insert(self->variables, name, dse_duplicate(d), (void **)&dse);
      if (dse) {
         if (dse->type == dEVENT)
            ev_release(dse->value.ev_v);
         dse_free(dse);
      }
      /* now execute program code */
      reset(mc.stack);
      mc.currentTopic = ev_topic(current);
      if (! setjmp(begin)) {
         (void)pthread_setspecific(jmpbuf_key, (void *)begin);
         execute(&mc, self->behav);
         execerr = 0;
      } else
         execerr = 1;
   }

/*
 * we arrive here for three possible reasons:
 * 1. self->must_exit was set to true, indicating that unregister was invoked
 * 2. execerr == 1 because of execution error in initialization code
 * 3. execerr == 1 because of execution error in behavior code
 *
 * still need to perform all of the cleanup; if execerr == 1, then must
 * send rpc to client application before disconnecting from the rpc connection
 */

   self->has_exited++;

/*
 * return storage associated with topic->variable mapping
 */

   n = tsht_keys(self->topics, &keys);
   for (i = 0; i < n; i++) {
      void *datum;
      pthread_mutex_unlock(&(self->lock));/* if top_pub blocked on our lock */
      top_unsubscribe(keys[i], self->id); /* unsubscribe from the topic */
      pthread_mutex_lock(&(self->lock));
      tsht_remove(self->topics, keys[i], &datum);
      mem_free(datum);			/* return the storage */
   }
   mem_free(keys);
   tsht_delete(self->topics);		/* delete the hash table */

/*
 * now release all queued events
 */

   while ((current = (Event *)ll_remove(self->events))) {
      ev_release(current);		/* decrement ref count */
   }
   ll_delete(self->events);		/* delete the linked list */

/*
 * now return storage associated with variable->value mapping
 */

   n = tsht_keys(self->variables, &keys);
   for (i = 0; i < n; i++) {
      DataStackEntry *dx;
      tsht_remove(self->variables, keys[i], (void **)&dx);
      if (dx->type == dEVENT)
         ev_release(dx->value.ev_v);
      else if (dx->type == dSEQUENCE || dx->type == dWINDOW)
         ;	/* assume seq and win inserted into map */
      else 
         freeDSE(dx);
      dse_free(dx);			/* return the storage */
   }
   mem_free(keys);
   tsht_delete(self->variables);	/* delete the hash table */

/*
 * now return initialization and behavior code sequences and stack
 */

   mem_free(self->init);
   mem_free(self->behav);
   stack_destroy(mc.stack);

/*
 * if execution error, send error message
 * disconnect rpc channel
 */

   if (execerr) {
      char *s = (char *)pthread_getspecific(execerr_key);
      Q_Decl(buf,1024);
      char resp[100];
      unsigned rlen, len;
      sprintf(buf, "100<|>%s execution error: %s<|>0<|>0<|>",
              (ifbehav) ? "behavior" : "initialization", s);
      mem_free(s);
      len = strlen(buf) + 1;
      (void) rpc_call(self->rpc, Q_Arg(buf), len, resp, sizeof(resp), &rlen);
      self->has_exited++;
   }
   pthread_mutex_unlock(&(self->lock));
   rpc_disconnect(self->rpc);
   sprintf(buf, "%08lx", self->id);
   (void) tsht_remove(automatons, buf, &dummy);
   mem_free(self);
   return NULL;
}

static void *timer_func(void *args) {
   struct timespec *delay = (struct timespec *)args;
   char *ts;

   while (1) {
      nanosleep(delay, NULL);
      ts = timestamp_to_string(timestamp_now());
      top_publish("Timer", ts);
      mem_free(ts);
   }
   return NULL;
}

#define ID_CNTR_START 12345;
#define ID_CNTR_LIMIT 2000000000
static unsigned long a_id_cntr = ID_CNTR_START;

static unsigned long next_id(void) {
   unsigned long ans = a_id_cntr++;
   if (a_id_cntr > ID_CNTR_LIMIT)
      a_id_cntr = ID_CNTR_START;
   return ans;
}

void au_init(void) {
   pthread_t th;
   (void)pthread_key_create(&jmpbuf_key, NULL);
   (void)pthread_key_create(&execerr_key, NULL);
   automatons = tsht_create(25L);
   (void) top_create("Timer", "1 tstamp/timestamp");
   (void) pthread_create(&th, NULL, timer_func, (void *)(&delay));
}

Automaton *au_create(char *program, RpcConnection rpc) {
   Automaton *au;
   void *dummy;
   pthread_t pthr;

   au = (Automaton *)mem_alloc(sizeof(Automaton));
   if (au) {
      au->id = next_id();
      au->must_exit = 0;
      au->has_exited = 0;
      pthread_mutex_init(&(au->lock), NULL);
      pthread_cond_init(&(au->cond), NULL);
      au->rpc = rpc;
      au->events = ll_create();
      if (au->events) {
         char buf[20];
         jmp_buf begin;
         pthread_mutex_lock(&compile_lock);
         if (!setjmp(begin)) {	/* not here because of a longjmp */
            (void)pthread_setspecific(jmpbuf_key, (void *)begin);
            ap_init(program);
            a_init();
            initcode();
            if (! a_parse()) {	/* successful compilation */
               size_t N;
               HTIterator *it;
               HTIterValue *itv;
               N = initSize * sizeof(InstructionEntry);
               if (N) {
                  au->init = (InstructionEntry *)mem_alloc(N);
                  memcpy(au->init, initialization, N);
               } else
                  au->init = NULL;
               N = behavSize * sizeof(InstructionEntry);
               au->behav = (InstructionEntry *)mem_alloc(N);
               memcpy(au->behav, behavior, N);
               au->variables = variables;
               au->topics = topics;
               sprintf(buf, "%08lx", au->id);
               (void) tsht_insert(automatons, buf, au, &dummy);
               pthread_mutex_lock(&(au->lock));
               it = tsht_iter_create(topics);
               if (it) {
                  while ((itv = tsht_iter_next(it))) 
                     top_subscribe(itv->key, au->id);
                  tsht_iter_delete(it);
               }
               variables = NULL;
               topics = NULL;
               pthread_mutex_unlock(&(au->lock));
               pthread_create(&pthr, NULL, thread_func, (void *)au);
               pthread_mutex_unlock(&compile_lock);
               return au;
            }
         } /* need else here to set up appropriate status return */
         pthread_mutex_unlock(&compile_lock);
      }
      ll_delete(au->events);
   }
   mem_free((void *)au);
   return NULL;
}

int au_destroy(unsigned long id) {
   char buf[20];
   Automaton *au;
   int ans = 0;
   sprintf(buf, "%08lx", id);
   au = tsht_lookup(automatons, buf);
   if (au) {
      void *dummy;
      int must_free = 0;
      (void) tsht_remove(automatons, buf, &dummy);
      pthread_mutex_lock(&(au->lock));
      if (! au->has_exited) {
         au->must_exit = 1;
         pthread_cond_signal(&(au->cond));
         ans = 1;
      } else {
         must_free = 1;
      }
      pthread_mutex_unlock(&(au->lock));
      if (must_free)
         mem_free(au);
   }
   return ans;
}

void au_publish(unsigned long id, Event *event) {
   Automaton *au = au_au(id);
   if (! au)
      return;	/* probably need to ev_release event here */
   pthread_mutex_lock(&(au->lock));
   if (! au->must_exit && ! au->has_exited) {
      ll_add2tail(au->events, event);
      pthread_cond_signal(&(au->cond));
   }
   pthread_mutex_unlock(&(au->lock));
}

unsigned long au_id(Automaton *au) {
   return au->id;
}

Automaton *au_au(unsigned long id) {
   char buf[20];
   sprintf(buf, "%08lx", id);
   return tsht_lookup(automatons, buf);
}

RpcConnection au_rpc(Automaton *au) {
   return au->rpc;
}
