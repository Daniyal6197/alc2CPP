#ifndef _HTTPACCUMULATOR_H_
#define _HTTPACCUMULATOR_H_

#include <netinet/ip.h> /* defines in_addr */

#define HST_LEN 128 /* cf. httplogger.c */
#define URI_LEN 128
#define CNT_LEN 128

typedef struct flow_key {
   struct in_addr ip_src;
   struct in_addr ip_dst;
   unsigned short sport;
   unsigned short dport;
   unsigned char proto;
} FlowKey;

typedef struct url_elem {
   struct url_elem *next;
   FlowKey id;
   /* Host; URI; Content-Type */
   char hst[HST_LEN];
   char uri[URI_LEN];
   char cnt[CNT_LEN];
} UrlElem;

/* initialize the data structures */
void url_init();

/* http sniffer call */
void url_update(FlowKey *key, char *hst, char *uri, char *cnt);

/* swap accumulator tables, returning previous table for processing */
UrlElem **url_swap(int *size, unsigned long *nelems);

/* add chain of elements to free list */
void url_freeChain(UrlElem *elem);

#endif /* _HTTPACCUMULATOR_H_ */
