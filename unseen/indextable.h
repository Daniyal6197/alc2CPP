#ifndef _INDEXTABLE_H_
#define _INDEXTABLE_H_

#include "sqlstmts.h"
#include "rtab.h"
#include "list.h"
#include "srpc.h"
#include "table.h"
#include "node.h"
#include "pubsub.h"

typedef struct indextable Indextable;

Indextable    *itab_new(void);
int           itab_create_table(Indextable *itab, char *tablename, int ncols,
                                char **colnames, int **coltypes,
                                short tabletype, short primary_column);
int           itab_update_table(Indextable *itab, sqlupdate *update);
int           itab_is_compatible(Indextable *itab, char *tablename, 
                                 int ncols, int **coltypes);
int           itab_table_exists(Indextable *itab, char *tablename);
Table         *itab_table_lookup(Indextable *itab, char *tablename);
int           itab_colnames_match(Indextable *itab, char *tablename,
                                  sqlselect *select);
Rtab          *itab_build_results(Indextable *itab, char *tablename,
                                  sqlselect *select);
Rtab          *itab_showtables(Indextable *itab);
unsigned long itab_subscribe(Indextable *itab, char *tablename,
                             sqlsubscribe *subscribe, RpcConnection rpc);
int           itab_unsubscribe(Indextable *itab, unsigned long id,
                               Subscription *sub);
List          *itab_get_subscriberlist(Indextable *itab, char *tablename);
void          itab_lock(Indextable *itab);
void          itab_unlock(Indextable *itab);
void          itab_lock_table(Indextable *itab, char *tablename);
void          itab_unlock_table(Indextable *itab, char *tablename);
Node          *itab_is_constrained(Indextable *itab, char *tablename,
                                   char **colvals);

#endif /* _INDEXTABLE_H_ */
