/*
 * tables for mapping from id to subscriptions and vice versa
 */
#include "pubsub.h"
#include "hashtable.h"
#include "mem.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#define SUBSCRIPTION_BUCKETS 20

static Hashtable *id2sub = NULL;
static Hashtable *sub2id = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int ps_create(long id, Subscription *s) {
   char buf[1024], *p;
   int ans = 0;

   pthread_mutex_lock(&mutex);
   if (id2sub == NULL) {
      id2sub = ht_create(SUBSCRIPTION_BUCKETS);
      sub2id = ht_create(SUBSCRIPTION_BUCKETS);
   }
   sprintf(buf, "%08lx", id);
   if (! ht_insert(id2sub, buf, (void *)s))
      goto done;
   if (! (p = str_dupl(buf))) {
      ht_remove(id2sub, buf);
      goto done;
   }
   sprintf(buf, "%s!%s!%s!%s", s->queryname, s->ipaddr, s->port, s->service);
   if (! ht_insert(sub2id, buf, p)) {
      ht_remove(id2sub, p);
      mem_free(p);
      goto done;
   }
   ans = 1;
done:
   pthread_mutex_unlock(&mutex);
   return ans;
}

void ps_delete(long id) {
   char buf[1024];
   Subscription *s;

   sprintf(buf, "%08lx", id);
   pthread_mutex_lock(&mutex);
   s = ht_lookup(id2sub, buf);
   if (s) {
      char *p;
      sprintf(buf, "%s!%s!%s!%s", s->queryname, s->ipaddr, s->port, s->service);
      p = ht_lookup(sub2id, buf);
      ht_remove(sub2id, buf);
      ht_remove(id2sub, p);
      mem_free(p);
   }
   pthread_mutex_unlock(&mutex);
}

Subscription *ps_id2sub(long id) {
   char buf[1024];
   Subscription *ans;

   sprintf(buf, "%08lx", id);
   pthread_mutex_lock(&mutex);
   ans = ht_lookup(id2sub, buf);
   pthread_mutex_unlock(&mutex);
   return ans;
}

int ps_sub2id(Subscription *s, long *id) {
   char *p, buf[1024];
   int ans = 0;

   sprintf(buf, "%s!%s!%s!%s", s->queryname, s->ipaddr, s->port, s->service);
   pthread_mutex_lock(&mutex);
   p = ht_lookup(sub2id, buf);
   if (p) {
      *id = strtol(p, (char **)NULL, 16);
      ans = 1;
   }
   pthread_mutex_unlock(&mutex);
   return ans;
}

int ps_exists(char *queryname, char *ipaddr, char *port, char *service) {
	
	char buf[1024];
	char *p;
	int ans = 0;

	if (sub2id == NULL)
		return ans;

	sprintf(buf, "%s!%s!%s!%s", queryname, ipaddr, port, service);
	pthread_mutex_lock(&mutex);
	p = ht_lookup(sub2id, buf);
	if (p) ans = 1;
	pthread_mutex_unlock(&mutex);
	return ans;
}

