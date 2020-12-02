#include "http_accumulator.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <arpa/inet.h>

#define ATABLE_SIZE  1001
#define NO_OF_ELEMS 10000

static int verbose = 0;

static UrlElem *t0[ATABLE_SIZE]; /* one table */
static UrlElem *t1[ATABLE_SIZE]; /* the other table */
static UrlElem **ptrs[2] = {t0, t1}; /* current table to use */
static int ndx; /* index into ptrs[] */
static struct table {
	unsigned long nelems;
	UrlElem **ptrs;
} theTable;
static UrlElem elements[NO_OF_ELEMS]; /* initial elements on free list */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static UrlElem *freel = NULL; /* pointer to free list */

#define SHIFT 13
static unsigned int url_hash(UrlElem *u) {
	unsigned int ans = 9;
	unsigned char *s = (unsigned char *) u;
	unsigned int i;
	for (i = 0; i < sizeof(UrlElem); i++)
		ans = (SHIFT * ans) + *s++;
	return ans;
}

static void url_clear(UrlElem *u) {
	memset(&(u->id), 0, sizeof(FlowKey));
	/* clear strings */
	memset(&(u->hst), 0, HST_LEN);
	memset(&(u->uri), 0, URI_LEN);
	memset(&(u->cnt), 0, CNT_LEN);
}

static void url_free(UrlElem *u) {
	url_clear(u);
	u->next = freel;
	freel = u;
}

static UrlElem *url_alloc() {
	UrlElem *p = freel;
	if (p)
		freel = p->next;
	else if ((p = (UrlElem *)malloc(sizeof(UrlElem))))
		url_clear(p);
	return p;
}

void url_init() {
	int i;
	ndx = 0;
	theTable.ptrs = ptrs[ndx];
	theTable.nelems = 0;
	for (i = 0; i < ATABLE_SIZE; i++)
		theTable.ptrs[i] = NULL;
	freel = NULL; /* build free list */
	for (i = 0; i < NO_OF_ELEMS; i++) {
		url_free(&elements[i]);
	}
}

UrlElem **url_swap(int *size, unsigned long *nelems) {
	UrlElem **result;
	int i;
	pthread_mutex_lock(&mutex);
	result = theTable.ptrs;
	*nelems = theTable.nelems;
	*size = ATABLE_SIZE;
	i = ++ndx % 2;
	theTable.ptrs = ptrs[i];
	theTable.nelems = 0;
	for (i = 0; i < ATABLE_SIZE; i++)
		theTable.ptrs[i] = NULL;
	pthread_mutex_unlock(&mutex);
	return result;
}

static int url_lookup(UrlElem *u) {
	unsigned int ndx = url_hash(u) % ATABLE_SIZE;
	UrlElem *p;
	for (p = theTable.ptrs[ndx]; p != NULL; p = p->next) {
		if ((memcmp(&(u->id), &(p->id), sizeof(FlowKey)) == 0) && 
		(memcmp(u->hst, p->hst, HST_LEN) == 0) &&
		(memcmp(u->uri, p->uri, URI_LEN) == 0) &&
		(memcmp(u->cnt, p->cnt, CNT_LEN) == 0)) 
			return 0;
	}
	return 1;
}

static void url_element_dump(UrlElem *p) {
	FlowKey *f = &(p->id);
	printf("%u:", f->proto);
	printf("%s:%05hu:", inet_ntoa(f->ip_src), f->sport);
	printf("%s:%05hu:", inet_ntoa(f->ip_dst), f->dport);
	printf("%s:%s:%s\n", p->hst, p->uri, p->cnt);
}

void url_update(FlowKey *key, char *hst, char *uri, char *cnt) {
	UrlElem *p;	
	FlowKey t;
	unsigned int ndx;
	memset(&t, 0, sizeof(FlowKey));	/* clear the bytes */
	memcpy(&t, key, sizeof(FlowKey));
	pthread_mutex_lock(&mutex);	
	p = url_alloc();
	if (p) {
	memcpy(&(p->id), &t, sizeof(FlowKey));
	memcpy(&(p->hst), hst, HST_LEN);
	memcpy(&(p->uri), uri, URI_LEN);
	memcpy(&(p->cnt), cnt, CNT_LEN);
	if (verbose > 0) url_element_dump(p);
	/* ought to lookup table here for duplicates */
	if (url_lookup(p) == 0) {
		fprintf(stderr, "warning: <Flow, URL> already exists\n");
		pthread_mutex_unlock(&mutex);		
		return;
	}
	ndx = url_hash(p) % ATABLE_SIZE;
	p->next = theTable.ptrs[ndx];
	theTable.ptrs[ndx] = p;
	theTable.nelems++;
	}
	pthread_mutex_unlock(&mutex);    
}

void url_dump() {
	int i;
	pthread_mutex_lock(&mutex);
	printf("%ld URLs in the accumulator\n", theTable.nelems);
	for (i = 0; i < ATABLE_SIZE; i++) {
		UrlElem *p;
		for (p = theTable.ptrs[i]; p != NULL; p++)
			url_element_dump(p);
	}
	pthread_mutex_unlock(&mutex);
}

void url_freeChain(UrlElem *elem) {
	UrlElem *p, *q;
	pthread_mutex_lock(&mutex);
	p = elem;
	while (p != NULL) {
		q = p->next;
		url_free(p);
		p = q;
	}
	pthread_mutex_unlock(&mutex);
}

