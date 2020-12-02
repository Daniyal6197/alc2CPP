#include "linkedlist.h"
#include "mem.h"
#include <pthread.h>

#define FREE_LIST_INCREMENT 4096	/* number of entries to add to
                                           free list when depleted */

/*
 * private structures used to represent a linked list and list entries
 */
typedef struct l_entry {
   struct l_entry *next;
   void *datum;
} L_Entry;

struct linkedlist {
   unsigned long size;
   L_Entry *head;
   L_Entry *tail;
};

struct lliterator {
   L_Entry *next;
};

static L_Entry *freeList = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* assumes that mutex is held */
static void addToFreeList(L_Entry *p) {
   p->datum = NULL;
   p->next = freeList;
   freeList = p;
}

static void putEntry(L_Entry *p) {
   pthread_mutex_lock(&mutex);
   addToFreeList(p);
   pthread_mutex_unlock(&mutex);
}

static L_Entry *getEntry(void) {
   L_Entry *p;

   pthread_mutex_lock(&mutex);
   if (!(p = freeList)) {
      p = (L_Entry *)mem_alloc(FREE_LIST_INCREMENT * sizeof(L_Entry));
      if (p) {
         int i;

         for (i = 0; i < FREE_LIST_INCREMENT; i++, p++)
            addToFreeList(p);
         p = freeList;
      }
   }
   if (p)
      freeList = p->next;
   pthread_mutex_unlock(&mutex);
   return p;
}

LinkedList *ll_create(void) {
   LinkedList *ll;

   ll = (LinkedList *)mem_alloc(sizeof(LinkedList));
   if (ll != NULL) {
      ll->size = 0l;
      ll->head = NULL;
      ll->tail = NULL;
   }
   return ll;
}

unsigned long ll_length(LinkedList *ll) {
   return ll->size;
}

int ll_add2head(LinkedList *ll, void *datum) {
   L_Entry *e = getEntry();

   if (e != NULL) {
      e->datum = datum;
      e->next = ll->head;
      ll->head = e;
      if (!ll->size++)
         ll->tail = e;
      return 1;
   }
   return 0;
}

int ll_add2tail(LinkedList *ll, void *datum) {
   L_Entry *e = getEntry();

   if (e != NULL) {
      e->datum = datum;
      e->next = NULL;
      if (!ll->size++)
         ll->head = e;
      else
         ll->tail->next = e;
      ll->tail = e;
      return 1;
   }
   return 0;
}

void *ll_remove(LinkedList *ll) {
   void *datum = NULL;

   if (ll->size) {
      L_Entry *e = ll->head;
      datum = e->datum;
      ll->head = e->next;
      putEntry(e);
      if (! --ll->size)
         ll->tail = NULL;
   }
   return datum;
}

void ll_delete(LinkedList *ll) {
   L_Entry *p, *q;

   for (p = ll->head; p != NULL; p = q) {
      q = p->next;
      putEntry(p);
   }
   mem_free((void *)ll);
}

LLIterator *ll_iter_create(LinkedList *ll) {
   LLIterator *iter = (LLIterator *)mem_alloc(sizeof(LLIterator));

   if (iter) {
      iter->next = ll->head;
   }
   return iter;
}

void *ll_iter_next(LLIterator *iter) {
   void *datum = NULL;

   if (iter->next) {
      datum = iter->next->datum;
      iter->next = iter->next->next;
   }
   return datum;
}

void ll_iter_delete(LLIterator *iter) {
   mem_free((void *)iter);
}
