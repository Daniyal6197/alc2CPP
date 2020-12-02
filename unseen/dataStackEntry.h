#ifndef _DATASTACKENTRY_H_
#define _DATASTACKENTRY_H_

#include "tshtable.h"
#include "linkedlist.h"

/*
 * the data stack for the event processor is an array of the following structs
 */

#define dBOOLEAN   1
#define dINTEGER   2
#define dDOUBLE    3
#define dTSTAMP    4
#define dSTRING    5
#define dEVENT     6
#define dMAP	   7
#define dIDENT     8
#define dTIDENT    9
#define dWINDOW	  10
#define dITERATOR 11
#define dSEQUENCE 12
#define dPTABLE   13

#define DUPLICATE 1	/* bit indicating that it must be duplicated */
#define MUST_FREE 2	/* bit indicating that must free when done with it */
#define NOTASSIGN 4	/* bit indicating that it cannot be assigned*/

#define dROWS 21
#define dSECS 22

#define initDSE(d,t,f) {(d)->type=(t); (d)->flags=(f); }

typedef struct map {
   int type;
   TSHTable *ht;
} Map;

typedef struct window {
   int dtype;
   int wtype;
   unsigned int rows_secs;
   LinkedList *ll;
} Window;

typedef struct iterator {
   int type;
   int dtype;		/* needed for window iterator */
   int next;
   int size;
   union {
      char **m_idents;
      LLIterator *w_it;
   } u;
} Iterator;

typedef struct sequence Sequence;

typedef struct dataStackEntry {
   unsigned short type, flags;
   union {
      int bool_v;
      signed long long int_v;
      double dbl_v;
      unsigned long long tstamp_v;
      char *str_v;
      void *ev_v;
      Map *map_v;
      Window *win_v;
      Iterator *iter_v;
      Sequence *seq_v;
      struct dataStackEntry *next;	/* used to link onto free list */
   } value;
} DataStackEntry;

typedef struct windowEntry {
   unsigned long long tstamp;
   DataStackEntry dse;
} WindowEntry;

struct sequence {
   int used;
   int size;
   DataStackEntry *entries;
};

#endif /* _DATASTACKENTRY_H_ */
