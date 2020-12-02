#ifndef _HWDB_H_
#define _HWDB_H_

#include "rtab.h"
#include "pubsub.h"
#include "srpc.h"
#include "table.h"
#include "automaton.h"
#include "sqlstmts.h"

#define SUBSCRIPTION 1
#define REGISTRATION 2

typedef struct callBackInfo {
   short type, ifdisconnect;	/* value of SUBSCRIPTION or REGISTRATION */
   union {
      long id;
      Automaton *au;
   } u;
} CallBackInfo;

int   hwdb_init(int usesRPC);
Rtab  *hwdb_exec_query(char *query, int isreadonly);
int   hwdb_send_event(Automaton *au, char *buf, int ifdisconnect);
Table *hwdb_table_lookup(char *name);
void  hwdb_queue_cleanup(CallBackInfo *info);
int   hwdb_insert(sqlinsert *insert);

#endif /* _HWDB_H_ */
