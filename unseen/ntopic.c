/*
 * Topic ADT implementation
 */
#include "topic.h"
#include "event.h"
#include "automaton.h"
#include "tshtable.h"
#include "linkedlist.h"
#include "dataStackEntry.h"
#include "mem.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define DEFAULT_STREAM_TABLE_SIZE 20

typedef struct topic {
   int ncells;
   SchemaCell *schema;
   LinkedList *regAUs;
   pthread_mutex_t lock;
} Topic;

static TSHTable *topicTable;

void top_init(void) {
   topicTable = tsht_create(DEFAULT_STREAM_TABLE_SIZE);
}

int top_exist(char *name) {

   return (tsht_lookup(topicTable, name) != NULL);
}

static SchemaCell *unpack(char *schema, int *ncells) {
   SchemaCell *ans;
   char *p = schema;
   int ncols = 0;

   while (isdigit((int)*p))
      ncols = 10 * ncols + (*p++ - '0');
   *ncells = ncols;
   ans = (SchemaCell *)mem_alloc(ncols * sizeof(SchemaCell));
   if (ans) {
      int i, theType;
      char *name, *type;

      for (i = 0; i < ncols; i++) {
         while (isspace((int)*p))
            p++;
         name = p;
         while (*p != '/')
            p++;
         *p++ = '\0';
         type = p;
         while (isalpha((int)*p))
            p++;
         *p++ = '\0';
         if (strcmp(type, "boolean") == 0)
            theType = dBOOLEAN;
         else if (strcmp(type, "integer") == 0)
            theType = dINTEGER;
         else if (strcmp(type, "real") == 0)
            theType = dDOUBLE;
         else if (strcmp(type, "varchar") == 0)
            theType = dSTRING;
         else if (strcmp(type, "timestamp") == 0)
            theType = dTSTAMP;
         else {
            break;
         }
         ans[i].name = (char *)mem_alloc(strlen(name) + 1);
         strcpy(ans[i].name, name);
         ans[i].type = theType;
      }
      if (i < ncols) {
         int j;
         for (j = 0; j < i; j++)
            mem_free(ans[j].name);
         mem_free(ans);
         ans = NULL;
      }
   }
   return ans;
}

int top_create(char *name, char *schema) {
   Topic *st;
   int ncells;
   char bf[1024];

   if (tsht_lookup(topicTable, name) != NULL)	/* topic already defined */
      return 0;
   st = (Topic *)mem_alloc(sizeof(Topic));
   if (st != NULL) {
      void *dummy;
      strcpy(bf, schema);
      pthread_mutex_init(&(st->lock), NULL);
      st->schema = unpack(bf, &ncells);
      if (st->schema != NULL) {
         st->ncells = ncells;
         st->regAUs = ll_create();
         if (st->regAUs != NULL) {
            if (tsht_insert(topicTable, name, st, &dummy))
               return 1;
            ll_delete(st->regAUs);
         }
         mem_free((void *)(st->schema));
      }
      mem_free((void *)st);
   }
   return 0;
}

int top_publish(char *name, char *message) {
   int ret = 0;
   Topic *st;

   if ((st = (Topic *)tsht_lookup(topicTable, name))) {
      ret = 1;
      pthread_mutex_lock(&(st->lock));
      if (ll_length(st->regAUs) > 0) {
         LLIterator *iter;
         iter = ll_iter_create(st->regAUs);
         if (iter) {
            Event *event;
            event = ev_create(name, message, ll_length(st->regAUs));
            if (event) {
               unsigned long id;
               while ((id = (unsigned long)ll_iter_next(iter)) != 0) {
                  au_publish(id, event);
               }
            }
            ll_iter_delete(iter);
         }
      }
      pthread_mutex_unlock(&(st->lock));
   }
   return ret;
}

int top_subscribe(char *name, unsigned long id) {
   Topic *st = (Topic *)tsht_lookup(topicTable, name);

   if (st) {
      pthread_mutex_lock(&(st->lock));
      ll_add2head(st->regAUs, (void *)id);
      pthread_mutex_unlock(&(st->lock));
      return 1;
   }
   return 0;
}

void top_unsubscribe(char *name, unsigned long id) {
   Topic *st = (Topic *)tsht_lookup(topicTable, name);

   if (st) {
      unsigned long n;
      unsigned long entId;
      /* remove entry that matches au->id */
      pthread_mutex_lock(&(st->lock));
      n = ll_length(st->regAUs);
      while (n-- > 0) {
         entId = (unsigned long)ll_remove(st->regAUs);  /* fetch the first element */
	 if (entId == id)
            break;
	 ll_add2tail(st->regAUs, (void *)entId);
      }
      pthread_mutex_unlock(&(st->lock));
   }
}

int top_schema(char *name, int *ncells, SchemaCell **schema) {
   Topic *st = (Topic *)tsht_lookup(topicTable, name);

   if (st) {
      *ncells = st->ncells;
      *schema = st->schema;
      return 1;
   }
   return 0;
}

int top_index(char *topic, char *column) {
   Topic *st = (Topic *)tsht_lookup(topicTable, topic);

   if (st) {
      int i;
      for (i = 0; i < st->ncells; i++)
         if (strcmp(column, st->schema[i].name) == 0)
            return i;
   }
   return -1;
}
