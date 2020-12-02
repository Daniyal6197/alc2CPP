#ifndef _PUBSUB_H_
#define _PUBSUB_H_

#include "srpc.h"

typedef struct subscription {
   char *queryname;
   char *ipaddr;
   char *port;
   char *service;
   char *tablename;
   RpcConnection rpc;
} Subscription;

int          ps_create(long id, Subscription *s);
void         ps_delete(long id);
Subscription *ps_id2sub(long id);
int          ps_sub2id(Subscription *s, long *id);
int          ps_exists(char *qname, char *ipaddr, char *port, char *service);

#endif /* _PUBSUB_H_ */
