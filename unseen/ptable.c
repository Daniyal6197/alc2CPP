/*
 * PTable ADT implementation
 */
#include "ptable.h"
#include "tshtable.h"
#include "dataStackEntry.h"
#include "mem.h"
#include "hwdb.h"
#include "table.h"
#include "nodecrawler.h"
#include "topic.h"
#include "tuple.h"
#include "typetable.h"
#include <stdio.h>

#define DEFAULT_PERSISTENT_TABLE_SIZE 20

static TSHTable *persistentTables = NULL;
static int defined = 1;

static void ptab_init(void) {
   persistentTables = tsht_create(DEFAULT_PERSISTENT_TABLE_SIZE);
}

int ptab_exist(char *name) {
   if (! persistentTables)
      ptab_init();
   return (tsht_lookup(persistentTables, name) != NULL);
}

int ptab_create(char *name) {
   void *dummy;
   if (! persistentTables)
      ptab_init();
   if (tsht_lookup(persistentTables, name) != NULL)
      return 0;
   return tsht_insert(persistentTables, name, &defined, &dummy);
}

int ptab_hasEntry(char *name, char *ident) {
   Table *tn = hwdb_table_lookup(name);
   Nodecrawler *nc;
   int result;

   if (! tn || (! tn->tabletype))
       return 0;
   table_lock(tn);
   nc = nodecrawler_new(tn->oldest, tn->newest);
   result = (nodecrawler_find_value(nc, tn->primary_column, ident) != NULL);
   table_unlock(tn);
   nodecrawler_free(nc);
   return result;
}

Sequence *ptab_lookup(char *name, char *ident) {
   Sequence *ans = NULL;
   Table *tn = hwdb_table_lookup(name);
   Nodecrawler *nc;
   char bf[4096], *b = NULL;
   int i;
   Node *n;
   union Tuple *p;

   if (! tn || (! tn->tabletype))
      return ans;
   table_lock(tn);
   nc = nodecrawler_new(tn->oldest, tn->newest);
   if ((n = nodecrawler_find_value(nc, tn->primary_column, ident))) {
      p = (union Tuple *)(n->tuple);	/* convert it to a tuple */
      b = bf;
      for (i = tn->primary_column; i < tn->ncols; i++) {
         b += sprintf(b, "%s", (char *)(p->ptrs[i]));
	 *b++ = '\0';
      }
   }
   nodecrawler_free(nc);
   table_unlock(tn);
   if (b != NULL) {	/* non-primary key values in bf, generate Sequence */
      int ncols;
      SchemaCell *schema;
      (void) top_schema(name, &ncols, &schema);	/* obtain the table schema */
      ans = (Sequence *)mem_alloc(sizeof(Sequence));
      if (ans) {
         int nelems = ncols - tn->primary_column - 1;
         ans->entries = (DataStackEntry *)mem_alloc(nelems * sizeof(DataStackEntry));
	 if (ans->entries) {
            char *q = bf;
	    DataStackEntry *d = ans->entries;
	    int j;
            ans->used = nelems;
	    ans->size = nelems;
            for (i = tn->primary_column+1, j = 0; i < ncols; i++, j++) {
               b = q;
	       while (*q++) ;	/* skip to beginning of next value */
               d[j].type = schema[i].type;
	       d[j].flags = 0;
	       switch(d[j].type) {
               case dBOOLEAN:
                  d[j].value.bool_v = atoi(b); break;
               case dINTEGER:
                  d[j].value.int_v = atoll(b); break;
               case dDOUBLE:
                  d[j].value.dbl_v = atof(b); break;
               case dTSTAMP:
                  d[j].value.tstamp_v = string_to_timestamp(b); break;
               case dSTRING:
                  d[j].value.str_v = str_dupl(b); d[j].flags |= MUST_FREE;
		  break;
	       }
            }
	 } else {
            mem_free(ans);
	    ans = NULL;
	 }
      }
   }
   return ans;
}

static void packvalue(DataStackEntry *d, char *s) {
   switch(d->type) {
      case dINTEGER: sprintf(s, "%lld", d->value.int_v); break;
      case dDOUBLE: sprintf(s, "%.8f", d->value.dbl_v); break;
      case dSTRING: sprintf(s, "%s", d->value.str_v); break;
      case dTSTAMP: {
         char *p;
         p = timestamp_to_string(d->value.tstamp_v);
         sprintf(s, "%s", p);
         mem_free(p);
         break;
      }
      case dBOOLEAN: sprintf(s, "%s", (d->value.bool_v) ? "TRUE" : "FALSE"); break;
      default: sprintf(s, "WRONG DATA TYPE: %d", d->type);
   }
}

int ptab_update(char *name, char *ident, Sequence *value) {
   sqlinsert sqli;
   char **colval;
   int **coltype;
   int i, n;
   int ans;
   char buf[2048];
   DataStackEntry *dse;

   sqli.tablename = name;
   n = value->used;
   sqli.ncols = n;
   colval = (char **)mem_alloc(n * sizeof(char *));
   coltype = (int **)mem_alloc(n * sizeof(int *));
   //printf("update %s:", name);
   for (i = 0, dse = value->entries; i < n; i++, dse++) {
      packvalue(dse, buf);
      //printf(" %s,", buf);
      colval[i] = str_dupl(buf);
      switch (dse->type) {
      case dBOOLEAN:	coltype[i] = PRIMTYPE_BOOLEAN; break;
      case dINTEGER:	coltype[i] = PRIMTYPE_INTEGER; break;
      case dDOUBLE:	coltype[i] = PRIMTYPE_REAL; break;
      case dSTRING:	coltype[i] = PRIMTYPE_VARCHAR; break;
      case dTSTAMP:	coltype[i] = PRIMTYPE_TIMESTAMP; break;
      }
   }
   //printf("\n"); fflush(stdout);
   sqli.colval = colval;
   sqli.coltype = coltype;
   sqli.transform = 1;
   ans = hwdb_insert(&sqli);
   for (i = 0; i < n; i++)
      mem_free(colval[i]);
   mem_free(colval);
   mem_free(coltype);
   return ans;
}

int ptab_keys(char *name, char ***theKeys) {
   Table *tn = hwdb_table_lookup(name);
   int ans = -1;
   if (! tn || (! tn->tabletype))
      return ans;
   table_lock(tn);
   ans = tn->count;
   if (ans) {
      char **keys = (char **)mem_alloc(ans * sizeof(char *));
      if (! keys) {
         ans = -1;
      } else {
         Nodecrawler *nc;
         int i;
         Node *n;
         union Tuple *p;
         nc = nodecrawler_new(tn->oldest, tn->newest);
	 nodecrawler_set_to_start(nc);
	 for (i =0; nodecrawler_has_more(nc); i++) {
            n = nc->current;
	    p = (union Tuple *)(n->tuple);
	    keys[i] = str_dupl((char *)(p->ptrs[tn->primary_column]));
	    nodecrawler_move_to_next(nc);
	 }
	 nodecrawler_free(nc);
      }
      *theKeys = keys;
   } else {
      *theKeys = (char **)NULL;
   }
   table_unlock(tn);
   return ans;
}
