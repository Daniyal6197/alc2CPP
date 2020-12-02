/*
 * execution and stack manipulation routines
 */

#include "dataStackEntry.h"
#include "tshtable.h"
#include "code.h"
#include "event.h"
#include "topic.h"
#include "timestamp.h"
#include "mem.h"
#include "a_globals.h"
#include "dsemem.h"
#include "ptable.h"
#include "typetable.h"
#include "sqlstmts.h"
#include "hwdb.h"
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define NSTACK 2560
#define NPROG 20000

static InstructionEntry initbuf[NPROG];
static InstructionEntry behavbuf[NPROG];

int iflog = 0;
int initSize, behavSize;
InstructionEntry *progp, *startp;
InstructionEntry *initialization = initbuf;
InstructionEntry *behavior = behavbuf;

void dumpDataStackEntry (DataStackEntry *d, int verbose) {
   if (verbose)
      fprintf(stderr, "type = %d, value = ", d->type);
   switch(d->type) {
      char *ts;
      case dBOOLEAN:
         if (d->value.bool_v)
            fprintf(stderr, "T");
         else
            fprintf(stderr, "F");
         break;
      case dINTEGER:
         fprintf(stderr, "%lld", d->value.int_v);
         break;
      case dDOUBLE:
         fprintf(stderr, "%.8f", d->value.dbl_v);
         break;
      case dTSTAMP:
         ts = timestamp_to_string(d->value.tstamp_v);
         fprintf(stderr, "%s", ts);
         mem_free(ts);
         break;
      case dSTRING:
      case dIDENT:
      case dTIDENT:
         fprintf(stderr, "%s", d->value.str_v);
         break;
      case dEVENT:
         fprintf(stderr, "A_Tuple");
         break;
      case dMAP:
         fprintf(stderr, "A_Map");
         break;
      case dWINDOW:
         fprintf(stderr, "A_Window");
         break;
      case dITERATOR:
         fprintf(stderr, "An_Iterator");
         break;
      case dSEQUENCE:
         fprintf(stderr, "A_Sequence");
         break;
      case dPTABLE:
         fprintf(stderr, "A_PersistentTable");
         break;
   }
   if (verbose)
      fprintf(stderr, "\n");
}

DataStackEntry *dse_duplicate(DataStackEntry d) {
   DataStackEntry *p = dse_alloc();
   if (p)
      memcpy(p, &d, sizeof(DataStackEntry));
   return p;
}

Map *map_duplicate(Map m) {
   Map *p = (Map *)mem_alloc(sizeof(Map));
   if (p)
      memcpy(p, &m, sizeof(Map));
   return p;
}

Window *win_duplicate(Window w) {
   Window *p = (Window *)mem_alloc(sizeof(Window));
   if (p)
      memcpy(p, &w, sizeof(Window));
   return p;
}

WindowEntry *we_duplicate(WindowEntry w) {
   WindowEntry *p = (WindowEntry *)mem_alloc(sizeof(WindowEntry));
   if (p)
      memcpy(p, &w, sizeof(WindowEntry));
   return p;
}

Iterator *iter_duplicate(Iterator it) {
   Iterator *p = (Iterator *)mem_alloc(sizeof(Iterator));
   if (p)
      memcpy(p, &it, sizeof(Iterator));
   return p;
}

Sequence *seq_duplicate(Sequence s) {
   Sequence *p = (Sequence *)mem_alloc(sizeof(Sequence));
   if (p)
      memcpy(p, &s, sizeof(Sequence));
   return p;
}

void initcode(void) {
   progp = initialization;
   startp = progp;
   initSize = 0;
}

void switchcode(void) {
   initSize = (progp - startp);
   progp = behavior;
   startp = progp;
   behavSize = 0;
}

void endcode(void) {
   behavSize = (progp - startp);
}

InstructionEntry *code(int ifInst, Inst f, DataStackEntry *d, char *what) {
   InstructionEntry *savedprogp = progp;
   if (iflog) fprintf(stderr, "ifInst = %d, f = %p, d = %p, \"%s\"\n",
                      ifInst, f, d, what);
   if (progp >= startp+NPROG)
      execerror("Program too large", NULL);
   else {
      if (ifInst) {
         progp->type = FUNC;
         progp->u.op = f;
      } else {
         progp->type = DATA;
         progp->u.immediate = *d;
      }
      progp->label = what;
      progp++;
   }
   return savedprogp;
}

void execute(MachineContext *mc, InstructionEntry *p) {
   mc->pc = p;
   while (mc->pc->u.op != STOP) {
      (*((mc->pc++)->u.op))(mc);
   }
}

static void printIE(InstructionEntry *p, FILE *fd) {
   switch(p->type) {
      case FUNC:
         fprintf(fd, "Function pointer (%s) %p\n", p->label, p->u.op);
         break;
      case DATA:
         fprintf(fd, "Immediate data (%s), ", p->label);
         dumpDataStackEntry(&(p->u.immediate), 1);
         break;
      case PNTR:
         fprintf(fd, "Offset from PC (%s), %d\n", p->label, p->u.offset);
         break;
   }
}

static void dumpProgram(InstructionEntry *p) {
   if (! iflog)
      return;
   while (!(p->type == FUNC && p->u.op == STOP)) {
      printIE(p, stderr);
      p++;
   }
}

void dumpProgramBlock(InstructionEntry *p, int n) {
   int savediflog = iflog;
   iflog = 1;
   while (n > 0) {
      printIE(p, stderr);
      p++;
      n--;
   }
   iflog = savediflog;
}

void dumpCompilationResults(unsigned long id, TSHTable *v,
                            InstructionEntry *init, int initSize,
                            InstructionEntry *behav, int behavSize) {
   HTIterator *it;
   HTIterValue *itv;

   fprintf(stderr, "=== Compilation results for automaton %08lx\n", id);
   fprintf(stderr, "====== variables\n");
   it = tsht_iter_create(v);
   if (it) {
      while ((itv = tsht_iter_next(it))) {
         DataStackEntry *d = (DataStackEntry *)itv->datum;
         fprintf(stderr, "%s: ", itv->key);
         switch(d->type) {
            case dBOOLEAN: fprintf(stderr, "bool"); break;
            case dINTEGER: fprintf(stderr, "int"); break;
            case dDOUBLE: fprintf(stderr, "real"); break;
            case dTSTAMP: fprintf(stderr, "tstamp"); break;
            case dSTRING: fprintf(stderr, "string"); break;
            case dEVENT: fprintf(stderr, "tuple"); break;
            case dMAP: fprintf(stderr, "map"); break;
            case dIDENT: fprintf(stderr, "identifier"); break;
            case dWINDOW: fprintf(stderr, "window"); break;
            case dITERATOR: fprintf(stderr, "iterator"); break;
            case dSEQUENCE: fprintf(stderr, "sequence"); break;
            case dPTABLE: fprintf(stderr, "PTable"); break;
         }
         fprintf(stderr, "\n");
      }
      tsht_iter_delete(it);
   }
   fprintf(stderr, "====== initialization instructions\n");
   dumpProgramBlock(init, initSize);
   fprintf(stderr, "====== behavior instructions\n");
   dumpProgramBlock(behav, behavSize);
   fprintf(stderr, "=== End of comp results for automaton %08lx\n", id);
}

static void logit(char *s, Automaton *au) {
   fprintf(stderr, "%08lx: %s", au_id(au), s);
}

void constpush(MachineContext *mc) {
   if (iflog) {
      logit("constpush called, ", mc->au);
      dumpDataStackEntry(&(mc->pc->u.immediate), 1);
   }
   push(mc->stack, mc->pc->u.immediate);
   mc->pc++;
}

void varpush(MachineContext *mc) {
   if (iflog) {
      logit("varpush called, ", mc->au);
      dumpDataStackEntry(&(mc->pc->u.immediate), 1);
   }
   push(mc->stack, mc->pc->u.immediate);
   mc->pc++;
}

void add(MachineContext *mc) {
   DataStackEntry d1, d2;
   if (iflog) logit("add called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   if ((d1.type == d2.type)) {
      switch(d1.type) {
         case dINTEGER:
            d1.value.int_v += d2.value.int_v;
            break;
         case dDOUBLE:
            d1.value.dbl_v += d2.value.dbl_v;
            break;
         default:
            execerror("add of non-numeric types", NULL);
      }
   } else
      execerror("add of two different types", NULL);
   d1.flags = 0;
   push(mc->stack, d1);
}

void subtract(MachineContext *mc) {
   DataStackEntry d1, d2;
   if (iflog) logit("subtract called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   if ((d1.type == d2.type)) {
      switch(d1.type) {
         case dINTEGER:
            d1.value.int_v -= d2.value.int_v;
            break;
         case dDOUBLE:
            d1.value.dbl_v -= d2.value.dbl_v;
            break;
         default:
            execerror("subtract of non-numeric types", NULL);
      }
   } else
      execerror("subtract of two different types", NULL);
   d1.flags = 0;
   push(mc->stack, d1);
}

void multiply(MachineContext *mc) {
   DataStackEntry d1, d2;
   if (iflog) logit("multiply called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   if ((d1.type == d2.type)) {
      switch(d1.type) {
         case dINTEGER:
            d1.value.int_v *= d2.value.int_v;
            break;
         case dDOUBLE:
            d1.value.dbl_v *= d2.value.dbl_v;
            break;
         default:
            execerror("multiply of non-numeric types", NULL);
      }
   } else
      execerror("multiply of two different types", NULL);
   d1.flags = 0;
   push(mc->stack, d1);
}

void divide(MachineContext *mc) {
   DataStackEntry d1, d2;
   if (iflog) logit("divide called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   if ((d1.type == d2.type)) {
      switch(d1.type) {
         case dINTEGER:
            d1.value.int_v /= d2.value.int_v;
            break;
         case dDOUBLE:
            d1.value.dbl_v /= d2.value.dbl_v;
            break;
         default:
            execerror("divide of non-numeric types", NULL);
      }
   } else
      execerror("divide of two different types", NULL);
   d1.flags = 0;
   push(mc->stack, d1);
}

void modulo(MachineContext *mc) {
   DataStackEntry d1, d2;
   if (iflog) logit("modulo called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   if ((d1.type == d2.type)) {
      switch(d1.type) {
         case dINTEGER:
            d1.value.int_v %= d2.value.int_v;
            break;
         default:
            execerror("modulo of non-integer types", NULL);
      }
   } else
      execerror("modulo of two different types", NULL);
   d1.flags = 0;
   push(mc->stack, d1);
}

void bitOr(MachineContext *mc) {
   DataStackEntry d1, d2;
   if (iflog) logit("bitOr called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   if ((d1.type == d2.type)) {
      switch(d1.type) {
         case dINTEGER:
            d1.value.int_v |= d2.value.int_v;
            break;
         default:
            execerror("bitOr of non-integer types", NULL);
      }
   } else
      execerror("bitOr of two different types", NULL);
   d1.flags = 0;
   push(mc->stack, d1);
}

void bitAnd(MachineContext *mc) {
   DataStackEntry d1, d2;
   if (iflog) logit("bitAnd called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   if ((d1.type == d2.type)) {
      switch(d1.type) {
         case dINTEGER:
            d1.value.int_v &= d2.value.int_v;
            break;
         default:
            execerror("bitAnd of non-integer types", NULL);
      }
   } else
      execerror("bitAnd of two different types", NULL);
   d1.flags = 0;
   push(mc->stack, d1);
}

void negate(MachineContext *mc) {
   DataStackEntry d;
   if (iflog) logit("negate called\n", mc->au);
   d = pop(mc->stack);
   switch(d.type) {
      case dINTEGER:
         d.value.int_v = -d.value.int_v;
         break;
      case dDOUBLE:
         d.value.dbl_v = -d.value.dbl_v;
         break;
      default:
         execerror("negate of non-numeric type", NULL);
   }
   d.flags = 0;
   push(mc->stack, d);
}

static void printvalue(DataStackEntry d, FILE *fd) {
   char b[100];
   switch(d.type) {
      case dINTEGER:
         fprintf(fd, "%lld", d.value.int_v);
         break;
      case dDOUBLE:
         sprintf(b, "%.8f", d.value.dbl_v);
         fprintf(fd, "%s", b);
         if (strchr(b, '.') == NULL)
            fprintf(fd, ".0");
         break;
      case dSTRING:
      case dIDENT:
         fprintf(fd, "%s", d.value.str_v);
         break;
      case dTSTAMP: {
         char *p;
         p = timestamp_to_string(d.value.tstamp_v);
         fprintf(fd, "%s", p);
         mem_free(p);
         break;
      }
      case dBOOLEAN: {
         fprintf(fd, "%s", (d.value.bool_v) ? "true" : "false");
         break;
      }
      case dSEQUENCE: {
         int i;
         DataStackEntry *dse = d.value.seq_v->entries;
         fprintf(fd, "{");
         for (i = 0; i < d.value.seq_v->used; i++) {
            fprintf(fd, "%s", (i == 0) ? " " : ", ");
            printvalue(*dse++, stdout);
         }
         fprintf(fd, " }");
         break;
      }
      default:
         fprintf(fd, "unknown type: %d", d.type);
   }
}

void printDSE(DataStackEntry *d, FILE *fd) {
   printvalue(*d, fd);
}

static int packvalue(DataStackEntry d, char *s) {
   int n;
   switch(d.type) {
      case dINTEGER: n = sprintf(s, "%lld", d.value.int_v); break;
      case dDOUBLE: n = sprintf(s, "%.8f", d.value.dbl_v); break;
      case dSTRING: n = sprintf(s, "%s", d.value.str_v); break;
      case dTSTAMP: {
         char *p;
         p = timestamp_to_string(d.value.tstamp_v);
         n = sprintf(s, "%s", p);
         mem_free(p);
         break;
      }
      case dBOOLEAN:
         n = sprintf(s, "%s", (d.value.bool_v) ? "true" : "false");
         break;
   }
   return n;
}

/* free any heap storage associated with temporary data stack entry */
void freeDSE(DataStackEntry *d) {
   switch(d->type) {
      case dSTRING:
      case dIDENT: {
         if (d->value.str_v && (d->flags & MUST_FREE))
            mem_free(d->value.str_v);
         break;
      }
      case dITERATOR: {
         if (d->value.iter_v) {
            Iterator *it = d->value.iter_v;
            if (it->type == dMAP && it->u.m_idents)
               mem_free(it->u.m_idents);
            else if (it->type == dPTABLE && it->u.m_idents) {
               int i;
               for (i = 0; i < it->size; i++)
                  mem_free(it->u.m_idents[i]);
               mem_free(it->u.m_idents);
            } else if (it->u.w_it)
               ll_iter_delete(it->u.w_it);
            mem_free(d->value.iter_v);
         }
         break;
      }
      case dSEQUENCE: {
         if (d->value.seq_v) {
            Sequence *seq = d->value.seq_v;
            if (seq->entries) {
               int i;
               DataStackEntry *dse = seq->entries;
               for (i = 0; i < seq->used; i++)
                  freeDSE(dse++);
               mem_free(seq->entries);
            }
            mem_free(seq);
         }
         break;
      }
      case dMAP: {
         if (d->value.map_v) {
            if (d->value.map_v->ht) {
               char **keys;
               int i, n;
               void *datum;
               n = tsht_keys(d->value.map_v->ht, &keys);
               for (i = 0; i < n; i++) {
                  DataStackEntry *dx;
                  (void)tsht_remove(d->value.map_v->ht, keys[i], &datum);
                  dx = (DataStackEntry *)datum;
                  freeDSE(dx);
                  dse_free(dx);
               }
               mem_free(keys);
               (void)tsht_delete(d->value.map_v->ht);
            }
            mem_free(d->value.map_v);
         }
         break;
      }
      case dWINDOW: {
         if (d->value.win_v) {
            if (d->value.win_v->ll) {
               Window *w = d->value.win_v;
               WindowEntry *we;

               while ((we = (WindowEntry *)ll_remove(w->ll))) {
                  freeDSE(&(we->dse));
                  mem_free(we);
               }
               ll_delete(w->ll);
            }
            mem_free(d->value.win_v);
         }
         break;
      }
   }
}

void print(MachineContext *mc) {
   DataStackEntry d = pop(mc->stack);
   if (iflog) logit("print called\n", mc->au);
   printf("\t");
   switch(d.type) {
      case dINTEGER:
      case dDOUBLE:
      case dSTRING:
      case dIDENT:
      case dTSTAMP:
      case dBOOLEAN:
         printvalue(d, stdout);
         break;
      case dWINDOW: {
         Window *w = d.value.win_v;
         LLIterator *it = ll_iter_create(w->ll);
         WindowEntry *we;
         int n = 0;

         while ((we = (WindowEntry *)ll_iter_next(it))) {
            if (n++ > 0)
               printf(" ");
            switch(we->dse.type) {
            case dINTEGER:
            case dDOUBLE:
               printvalue(we->dse, stdout);
               break;
            default: printf("odd window type"); break;
            }
         }
         ll_iter_delete(it);
         break;
      }
      case dSEQUENCE: {
         Sequence *s = d.value.seq_v;
         int i;
         for (i = 0; i < s->used; i++) {
            if (i > 0)
               printf("<|>");
            printvalue(s->entries[i], stdout);
         }
         break;
      }
      default:
         printf("unknown type: %d\n", d.type);
   }
   printf("\n"); fflush(stdout);
   if (!(d.flags & NOTASSIGN))
      freeDSE(&d);
}

void destroy(MachineContext *mc) {
   DataStackEntry *v;
   DataStackEntry name = mc->pc->u.immediate;
   void *dummy;
   if (iflog) logit("destroy called\n", mc->au);
   v = (DataStackEntry *)tsht_lookup(mc->variables, name.value.str_v);
   /* void destroy(ident|iter|map|seq|win) */
   int t = v->type;
   if (t != dIDENT && t != dITERATOR && t != dMAP &&
       t != dSEQUENCE && t != dWINDOW && t != dPTABLE)
      execerror("attempt to destroy an instance of a basic type", 
                name.value.str_v);
   freeDSE(v);
   v->flags = 0;
   switch(v->type) {
      case dIDENT:    v->value.str_v = NULL; break;
      case dITERATOR: v->value.iter_v = NULL; break;
      case dMAP:      v->value.map_v = NULL; break;
      case dSEQUENCE: v->value.seq_v = NULL; break;
      case dWINDOW:   v->value.win_v = NULL; break;
      case dPTABLE:   v->value.str_v = NULL; break;
   }
   (void)tsht_insert(mc->variables, name.value.str_v, v, &dummy);
   mc->pc = mc->pc + 1;
}

void eval(MachineContext *mc) {
   DataStackEntry *v;
   DataStackEntry d = pop(mc->stack);
   if (iflog) logit("eval called\n", mc->au);

   if ((v = (DataStackEntry *)tsht_lookup(mc->variables, d.value.str_v)) == NULL)
      execerror("undefined variable", d.value.str_v);
   push(mc->stack, *v);
}

void extract(MachineContext *mc) {
   DataStackEntry *v, ndx, vbl;
   Event *t;
   if (iflog) logit("extract called\n", mc->au);

   ndx = pop(mc->stack);
   vbl = pop(mc->stack);
   v = (DataStackEntry *)tsht_lookup(mc->variables, vbl.value.str_v);
   if (v == NULL)
      execerror("undefined event variable: ", vbl.value.str_v);
   if (v->type != dEVENT)
      execerror("variable not an event: ", vbl.value.str_v);
   t = (Event *)(v->value.ev_v);
   if (! t)
      execerror("Tuple is null: ", vbl.value.str_v);
   (void) ev_theData(t, &v);
   push(mc->stack, v[ndx.value.int_v]);
}

static int simple_type(DataStackEntry *d) {
   int t = d->type;

   return (t == dBOOLEAN || t == dINTEGER || t == dDOUBLE || t == dTSTAMP);
}

void assign(MachineContext *mc) {
   DataStackEntry d1, d2, *d;
   void *v;
   int t;
   if (iflog) logit("assign called\n", mc->au);

   d1 = pop(mc->stack);
   d2 = pop(mc->stack);
   if (!simple_type(&d2) && (d2.flags & NOTASSIGN))
      execerror("attempt to create an alias: ", d1.value.str_v);
   t = d2.type;
   d = (DataStackEntry *)tsht_lookup(mc->variables, d1.value.str_v);
   if (t != d->type)
      execerror("lhs and rhs of assignment of different types", d1.value.str_v);
   if (d2.flags & DUPLICATE) {
      d2.value.str_v = str_dupl(d2.value.str_v);
      d2.flags &= ~DUPLICATE;		/* make sure duplicate flag false */
      d2.flags |= MUST_FREE;
   }
   d2.flags |= NOTASSIGN;		/* prevent aliasing */
   (void) tsht_insert(mc->variables, d1.value.str_v, dse_duplicate(d2), &v);
   if (v && t != dSEQUENCE && t != dWINDOW) {
      freeDSE((DataStackEntry *)v);
   }
   dse_free((DataStackEntry *)v);
}

static int compare(DataStackEntry *d1, DataStackEntry *d2) {
   int t1 = d1->type;
   int t2 = d2->type;
   int a = -1;                        /* assume d1 < d2 */

   if (t1 == t2) {
      switch(t1) {
         case dBOOLEAN: if (d1->value.bool_v == d2->value.bool_v)
                           a = 0;
                        else if (d1->value.bool_v > d2->value.bool_v)
                           a = 1;
                        break;
         case dINTEGER: if (d1->value.int_v == d2->value.int_v)
                           a = 0;
                        else if (d1->value.int_v > d2->value.int_v)
                           a = 1;
                        break;
         case dDOUBLE:  if (d1->value.dbl_v == d2->value.dbl_v)
                           a = 0;
                        else if (d1->value.dbl_v > d2->value.dbl_v)
                           a = 1;
                        break;
         case dTSTAMP:  if (d1->value.tstamp_v == d2->value.tstamp_v)
                           a = 0;
                        else if (d1->value.tstamp_v > d2->value.tstamp_v)
                           a = 1;
                        break;
         case dSTRING:  a = strcmp(d1->value.str_v, d2->value.str_v); break;
         default:
            execerror("attempting to compare structured datatypes", NULL);
            break;
      }
   } else
      execerror("attempting to compare different data types", NULL);
   return a;
}

void gt(MachineContext *mc) {
   DataStackEntry d1, d2;
   int a;
   if (iflog) logit("gt called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   a = compare(&d1, &d2);
   if (a > 0)
      d1.value.bool_v = 1;
   else
      d1.value.bool_v = 0;
   d1.type = dBOOLEAN;
   d1.flags = 0;
   push(mc->stack, d1);
}

void ge(MachineContext *mc) {
   DataStackEntry d1, d2;
   int a;
   if (iflog) logit("ge called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   a = compare(&d1, &d2);
   if (a >= 0)
      d1.value.bool_v = 1;
   else
      d1.value.bool_v = 0;
   d1.type = dBOOLEAN;
   d1.flags = 0;
   push(mc->stack, d1);
}

void lt(MachineContext *mc) {
   DataStackEntry d1, d2;
   int a;
   if (iflog) logit("lt called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   a = compare(&d1, &d2);
   if (a < 0)
      d1.value.bool_v = 1;
   else
      d1.value.bool_v = 0;
   d1.type = dBOOLEAN;
   d1.flags = 0;
   push(mc->stack, d1);
}

void le(MachineContext *mc) {
   DataStackEntry d1, d2;
   int a;
   if (iflog) logit("le called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   a = compare(&d1, &d2);
   if (a <= 0)
      d1.value.bool_v = 1;
   else
      d1.value.bool_v = 0;
   d1.type = dBOOLEAN;
   d1.flags = 0;
   push(mc->stack, d1);
}

void eq(MachineContext *mc) {
   DataStackEntry d1, d2;
   if (iflog) logit("eq called\n", mc->au);
   int a;
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   a = compare(&d1, &d2);
   if (a == 0)
      d1.value.bool_v = 1;
   else
      d1.value.bool_v = 0;
   d1.type = dBOOLEAN;
   d1.flags = 0;
   push(mc->stack, d1);
}

void ne(MachineContext *mc) {
   DataStackEntry d1, d2;
   int a;
   if (iflog) logit("ne called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   a = compare(&d1, &d2);
   if (a != 0)
      d1.value.bool_v = 1;
   else
      d1.value.bool_v = 0;
   d1.type = dBOOLEAN;
   d1.flags = 0;
   push(mc->stack, d1);
}

void and(MachineContext *mc) {
   DataStackEntry d1, d2;
   if (iflog) logit("and called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   if (d1.type == dBOOLEAN && d2.type == dBOOLEAN)
      if (d1.value.bool_v && d2.value.bool_v)
         d1.value.bool_v = 1;
      else
         d1.value.bool_v = 0;
   else
      execerror("AND of non boolean expressions", NULL);
   d1.flags = 0;
   push(mc->stack, d1);
}

void or(MachineContext *mc) {
   DataStackEntry d1, d2;
   if (iflog) logit("or called\n", mc->au);
   d2 = pop(mc->stack);
   d1 = pop(mc->stack);
   if (d1.type == dBOOLEAN && d2.type == dBOOLEAN)
      if (d1.value.bool_v || d2.value.bool_v)
         d1.value.bool_v = 1;
      else
         d1.value.bool_v = 0;
   else
      execerror("OR of non boolean expressions", NULL);
   d1.flags = 0;
   push(mc->stack, d1);
}

void not(MachineContext *mc) {
   DataStackEntry d;
   if (iflog) logit("not called\n", mc->au);
   d = pop(mc->stack);
   if (d.type == dBOOLEAN)
      d.value.bool_v = 1 - d.value.bool_v;
   else
      execerror("NOT of non-boolean expression", NULL);
   d.flags = 0;
   push(mc->stack, d);
}

void whilecode(MachineContext *mc) {
   DataStackEntry d;
   InstructionEntry *base = mc->pc - 1;	/* autoincrement happened before call */
   InstructionEntry *body = base + mc->pc->u.offset;
   InstructionEntry *nextStmt = base + (mc->pc+1)->u.offset;
   InstructionEntry *condition = mc->pc + 2;
   if (iflog) logit("whilecode called\n", mc->au);

   if (iflog) logit("=====> Dumping condition\n", mc->au);
   dumpProgram(condition);
   if (iflog) logit("=====> Dumping body\n", mc->au);
   dumpProgram(body);
   execute(mc, condition);
   d = pop(mc->stack);
   if (iflog)
      fprintf(stderr, "%08lx: condition is %d\n", au_id(mc->au), d.value.bool_v);
   while (d.value.bool_v) {
      execute(mc, body);
      execute(mc, condition);
      d = pop(mc->stack);
      if (iflog)
         fprintf(stderr, "%08lx: condition is %d\n", au_id(mc->au), d.value.bool_v);
   }
   mc->pc = nextStmt;
}

void ifcode(MachineContext *mc) {
   DataStackEntry d;
   InstructionEntry *base = mc->pc - 1;
   InstructionEntry *thenpart = base + mc->pc->u.offset;
   InstructionEntry *elsepart = base + (mc->pc+1)->u.offset;
   InstructionEntry *nextStmt = base + (mc->pc+2)->u.offset;
   InstructionEntry *condition = mc->pc + 3;

   if (iflog) {
      logit("ifcode called\n", mc->au);
      fprintf(stderr, "   PC        address %p\n", mc->pc);
      fprintf(stderr, "   condition address %p\n", condition);
      fprintf(stderr, "   thenpart  address %p\n", thenpart);
      fprintf(stderr, "   elsepart  address %p\n", elsepart);
      fprintf(stderr, "   nextstmt  address %p\n", nextStmt);
      logit("=====> Dumping condition\n", mc->au);
      dumpProgram(condition);
      logit("=====> Dumping thenpart\n", mc->au);
      dumpProgram(thenpart);
      if(elsepart != base) {
         logit("=====> Dumping elsepart\n", mc->au);
         dumpProgram(elsepart);
      } else
         logit("=====> No elsepart to if\n", mc->au);
   }
   execute(mc, condition);
   d = pop(mc->stack);
   if (iflog)
      fprintf(stderr, "%08lx: condition is %d\n", au_id(mc->au), d.value.bool_v);
   if (d.value.bool_v)
      execute(mc, thenpart);
   else if (elsepart != base)
      execute(mc, elsepart);
   mc->pc = nextStmt;
}

void newmap(MachineContext *mc) {
   Map m;
   DataStackEntry d;
   DataStackEntry type = mc->pc->u.immediate;
   if (iflog) logit("newmap called\n", mc->au);

   m.type = type.value.int_v;
   m.ht = tsht_create(0);
   d.type = dMAP;
   d.flags = 0;
   d.value.map_v = map_duplicate(m);
   push(mc->stack, d);
   mc->pc = mc->pc + 1;
}

void newwindow(MachineContext *mc) {
   Window w;
   DataStackEntry d;
   DataStackEntry dtype = mc->pc->u.immediate;
   DataStackEntry ctype = (mc->pc+1)->u.immediate;
   DataStackEntry csize = (mc->pc+2)->u.immediate;
   if (iflog) logit("newwindow called\n", mc->au);

   w.dtype = dtype.value.int_v;
   w.wtype = ctype.value.int_v;
   w.rows_secs = csize.value.int_v;
   w.ll = ll_create();
   d.type = dWINDOW;
   d.flags = 0;
   d.value.win_v = win_duplicate(w);
   push(mc->stack, d);
   mc->pc = mc->pc + 3;
}

static char *concat(int type, long long nargs, DataStackEntry *args) {
   char buf[1024], *p;
   char b[100];
   int i;

   p = buf;
   for (i = 0; i < nargs; i++) {
      char *q;
      if (type == dIDENT && i > 0)
         p = p + sprintf(p, "|");
      switch(args[i].type) {
      case dBOOLEAN:
         p = p + sprintf(p, "%s", (args[i].value.bool_v)?"T":"F"); break;
      case dINTEGER:
         p = p + sprintf(p, "%lld", args[i].value.int_v); break;
      case dDOUBLE:
         sprintf(b, "%.8f", args[i].value.dbl_v);
         if (strchr(b, '.') == NULL)
            strcat(b, ".0");
         p = p + sprintf(p, "%s", b); break;
      case dTSTAMP:
         q = timestamp_to_string(args[i].value.tstamp_v);
         p = p + sprintf(p, "%s", q);
         mem_free(q);
         break;
      case dIDENT:
         if (type == dIDENT)
            break;
      case dSTRING:
         p = p + sprintf(p, "%s", args[i].value.str_v); break;
      }
      if (!(args[i].flags & NOTASSIGN))
         freeDSE(&args[i]);
   }
   return str_dupl(buf);
}

static void lookup(DataStackEntry *table, char *id, DataStackEntry *d) {
   DataStackEntry *p;
   if (iflog) fprintf(stderr, "lookup entered.\n");
   p = (DataStackEntry *)tsht_lookup((table->value.map_v)->ht, id);
   if (p)
      *d = *p;
   else		/* id not defined, execerror */
      execerror(id, " not mapped to a value");
}

static void insert(DataStackEntry *table, char *id, DataStackEntry *datum) {
   void *olddatum;
   DataStackEntry *d;
   int t;
   if (iflog) fprintf(stderr, "insert entered.\n");
   if (table->type != dMAP)
      execerror("first argument to insert() must be a map", NULL);
   t = (table->value.map_v)->type;
   if (t != datum->type)
      execerror("Attempt to insert incorrect type into map", NULL);
   d = dse_duplicate(*datum);
   if (d->flags & DUPLICATE) {
      d->value.str_v = str_dupl(datum->value.str_v);
      d->flags = MUST_FREE;
   } else
      d->flags = 0;
   if (tsht_insert((table->value.map_v)->ht, id, d, &olddatum) && olddatum) {
      DataStackEntry *dse = (DataStackEntry *)olddatum;
      //if (dse->type != dSEQUENCE && dse->type != dWINDOW)
         freeDSE(dse);
      dse_free(dse);
   }
}

static void appendWindow(Window *w, DataStackEntry *d, DataStackEntry *ts) {
   WindowEntry we;
   tstamp_t first;
   int n;
   if (iflog) fprintf(stderr, "appendWindow entered.\n");
   if (w->dtype != d->type)
      execerror("type mismatch for window and value to append", NULL);
   we.dse = *d;
   if (we.dse.flags & DUPLICATE) {
      we.dse.value.str_v = str_dupl(d->value.str_v);
      we.dse.flags = MUST_FREE;
   } else
      we.dse.flags = 0;
   if (w->wtype == dSECS)
      we.tstamp = ts->value.tstamp_v;
   (void) ll_add2tail(w->ll, we_duplicate(we));
   n = 0;
   switch(w->wtype) {
      case dROWS:
         if (ll_length(w->ll) > w->rows_secs) {
            n++;
         }
         break;
      case dSECS: {
         LLIterator *it = ll_iter_create(w->ll);
         WindowEntry *p;
         first = timestamp_sub_incr(we.tstamp, (unsigned long)(w->rows_secs), 0);
         while ((p = (WindowEntry *)ll_iter_next(it))) {
            if (p->tstamp > first)
               break;
            n++;
         }
         ll_iter_delete(it);
         break;
      }
   }
   while (n-- >0) {
      DataStackEntry *dse = (DataStackEntry *)ll_remove(w->ll);
      freeDSE(dse);
      dse_free(dse);
   }
}

#define DEFAULT_SEQ_INCR 10

static void appendSequence(Sequence *s, int nargs, DataStackEntry *args){
   int i, ndx;
   DataStackEntry *d;
   if ((s->size - s->used) < nargs) {	/* have to grow the array */
      DataStackEntry *p, *q;
      int n = s->size + DEFAULT_SEQ_INCR;
      q = (DataStackEntry *)mem_alloc(n * sizeof(DataStackEntry));
      if (!q)
         execerror("allocation failure appending to sequence", NULL);
      p = s->entries;
      s->entries = q;
      s->size = n;
      n = s->used;
      while (n-- > 0)
         *q++ = *p++;
   }
   ndx = s->used;
   for (i = 0, d = args; i < nargs; i++, d++) {
      int t = d->type;
      DataStackEntry dse;
      if (t != dBOOLEAN && t != dINTEGER && t != dDOUBLE &&
          t != dSTRING && t != dTSTAMP)
         execerror("append: ", "restricted to basic types for a sequence");
      dse = *d;
      dse.flags = 0;
      if (t == dSTRING && (d->flags & DUPLICATE)) {
         dse.value.str_v = str_dupl(d->value.str_v);
         dse.flags = MUST_FREE;
      }
      s->entries[ndx++] = dse;
   }
   s->used = ndx;
}

static double average(Window *w) {
   LLIterator *it;
   WindowEntry *we;
   int N = 0;
   double sum = 0.0;
   if (iflog) fprintf(stderr, "average entered.\n");
   if (w->dtype != dINTEGER && w->dtype != dDOUBLE)
      execerror("average only legal on windows of ints or reals", NULL);
   it = ll_iter_create(w->ll);
   while ((we = (WindowEntry *)ll_iter_next(it))) {
      double x;
      if (w->dtype == dINTEGER)
         x =(double)((we->dse).value.int_v);
      else
         x = (we->dse).value.dbl_v;
      sum += x;
      N++;
   }
   ll_iter_delete(it);
   if (N == 0)
      return 0.0;
   else
      return (sum/(double)N);
}

static double std_dev(Window *w) {
   LLIterator *it;
   WindowEntry *we;
   int N = 0;
   double sum = 0.0;
   double sumsq = 0.0;
   if (iflog) fprintf(stderr, "average entered.\n");
   if (w->dtype != dINTEGER && w->dtype != dDOUBLE)
      execerror("average only legal on windows of ints or reals", NULL);
   it = ll_iter_create(w->ll);
   while ((we = (WindowEntry *)ll_iter_next(it))) {
      double x;
      if (w->dtype == dINTEGER)
         x =(double)((we->dse).value.int_v);
      else
         x = (we->dse).value.dbl_v;
      sum += x;
      sumsq += x * x;
      N++;
   }
   ll_iter_delete(it);
   if (N == 0 || N == 1)
      return 0.0;
   else
      return sqrt((sumsq - sum * sum / (double) N) / (double)(N-1));
}

static void sendevent(MachineContext *mc, long long nargs, DataStackEntry *args) {
   int i, n, types[50];
   char event[1024], *p;
   Q_Decl(result,2048);
   DataStackEntry d;

   if (iflog) logit("sendevent entered.\n", mc->au);
   n = 0;
   p = event;
   if (args[0].type != dTSTAMP && args[0].type != dEVENT) {
      types[n++] = dTSTAMP;
      d.type = dTSTAMP;
      d.value.tstamp_v = timestamp_now();
      p += packvalue(d, p);
      p += sprintf(p, "<|>");
   }
   for (i = 0; i < nargs; i++) {
      if (args[i].type == dSEQUENCE) {
         int j;
         Sequence *s = args[i].value.seq_v;
         for (j = 0; j < s->used; j++) {
            types[n++] = s->entries[j].type;
            p += packvalue(s->entries[j], p);
            p += sprintf(p, "<|>");
         }
      } else if (args[i].type == dEVENT) {
         int j;
         DataStackEntry *dse;
         Event *ev = (Event *)args[i].value.ev_v;
         int m = ev_theData(ev, &dse);
         for (j = 0; j < m; j++) {
            types[n++] = dse[j].type;
            p += packvalue(dse[j], p);
            p += sprintf(p, "<|>");
         }
      } else {
         types[n++] = args[i].type;
         p += packvalue(args[i], p);
         p += sprintf(p, "<|>");
      }
      if (!(args[i].type == dEVENT || (args[i].flags & NOTASSIGN)))
         freeDSE(&args[i]);
   }
   p = result;
   p += sprintf(p, "0<|>%lu<|>%d<|>1<|>\n", au_id(mc->au), n);
   for (i = 0; i < n; i++) {
      p += sprintf(p, "c%d:", i);
      switch(types[i]) {
         case dBOOLEAN: p += sprintf(p, "boolean"); break;
         case dINTEGER: p += sprintf(p, "integer"); break;
         case dDOUBLE: p += sprintf(p, "real"); break;
         case dTSTAMP: p += sprintf(p, "timestamp"); break;
         case dSTRING: p += sprintf(p, "varchar"); break;
      }
      p += sprintf(p, "<|>");
   }
   p += sprintf(p, "\n%s\n", event);
   if (! au_rpc(mc->au)) {
      printf("%s", result);
      fflush(stdout);
   } else {
      char resp[100];
      unsigned rlen, len = strlen(result) + 1;
      if (! rpc_call(au_rpc(mc->au), Q_Arg(result), len, resp, sizeof(resp), &rlen))
         execerror("callback RPC failed", NULL);
   }
}

static int *column_type(DataStackEntry dse) {
   int *ans = NULL;

   switch (dse.type) {
      case dBOOLEAN:	ans = PRIMTYPE_BOOLEAN; break;
      case dINTEGER:	ans = PRIMTYPE_INTEGER; break;
      case dDOUBLE:	ans = PRIMTYPE_REAL; break;
      case dSTRING:	ans = PRIMTYPE_VARCHAR; break;
      case dTSTAMP:	ans = PRIMTYPE_TIMESTAMP; break;
   }
   return ans;
}

#define NCOLUMNS 20

static void publishevent(MachineContext *mc, long long nargs,
                         DataStackEntry *args) {
   int i, n, ans, error;
   sqlinsert sqli;
   char *colval[NCOLUMNS];
   int *coltype[NCOLUMNS];
   char buf[2048];

   if (iflog) logit("publishevent entered.\n", mc->au);
   if (args[0].type != dSTRING)
      execerror("Incorrect first argument in call to publish", NULL);
   else if (! top_exist(args[0].value.str_v))
      execerror("topic does not exist :", args[0].value.str_v);
   sqli.tablename = args[0].value.str_v;
   sqli.transform = 0;
   n = -1;
   error = 0;
   for (i = 1; i < nargs; i++) {
      if (args[i].type == dSEQUENCE) {
         int j;
         Sequence *s = args[i].value.seq_v;
         for (j = 0; j < s->used; j++) {
            if (++n >= NCOLUMNS) {
               error++;
               goto done;
            }
            (void) packvalue(s->entries[j], buf);
            colval[n] = str_dupl(buf);
            coltype[n] = column_type(s->entries[j]);
         }
      } else {
         if (++n >= NCOLUMNS) {
            error++;
            goto done;
         }
         (void) packvalue(args[i], buf);
         colval[n] = str_dupl(buf);
         coltype[n] = column_type(args[i]);
      }
      if (!(args[i].flags & NOTASSIGN))
         freeDSE(&args[i]);
   }
done:
   if (! error) {
      n++;
      sqli.colval = colval;
      sqli.coltype = coltype;
      sqli.ncols = n;
      ans = hwdb_insert(&sqli);
   }
   for (i = 0; i < n; i++)
      mem_free(colval[i]);
   if (error)
      execerror("Too many arguments for topic: ", sqli.tablename);
   if (! ans)
      execerror("Error publishing event to topic: ", sqli.tablename);
}

static void doremove(DataStackEntry *table, char *id) {
   void *olddatum;
   if (iflog) fprintf(stderr, "remove entered.\n");
   if (tsht_remove((table->value.map_v)->ht, id, &olddatum) && olddatum) {
      DataStackEntry *d = (DataStackEntry *)olddatum;
      if (d->type != dSEQUENCE && d->type != dWINDOW)
         freeDSE(d);
      dse_free(olddatum);
   }
}

void procedure(MachineContext *mc) {
   DataStackEntry name = mc->pc->u.immediate;
   DataStackEntry narg = (mc->pc+1)->u.immediate;
   DataStackEntry args[MAX_ARGS];
   int i;
   unsigned int nargs;
   struct fpargs *range;

   nargs = narg.value.int_v;
   range = tsht_lookup(builtins, name.value.str_v);
   for (i = narg.value.int_v -1; i >= 0; i--)
      args[i] = pop(mc->stack);
   if (nargs < range->min || nargs > range->max)
      execerror("Incorrect # of arguments for procedure", name.value.str_v);
   if (iflog) {
      fprintf(stderr, "%08lx: %s(", au_id(mc->au), name.value.str_v);
      for (i = 0; i < narg.value.int_v; i++) {
         if (i > 0)
            fprintf(stderr, ", ");
         dumpDataStackEntry(args+i, 0);
      }
      fprintf(stderr, ")\n");
   }
   switch(range->index) {
   case 0: {		/* void topOfHeap() */
      mem_heap_end_address("Top of heap: ");
      break;
      }
   case 1: {		/* void insert(map, ident, map.type) */
      /* args[0] is map, args[1] is ident, args[2] is new value */
      if (args[0].type == dMAP) {
         insert(args+0, args[1].value.str_v, args+2);
      } else if (args[0].type == dPTABLE) {
         if (! ptab_update(args[0].value.str_v, args[1].value.str_v, args[2].value.seq_v))
            execerror(args[0].value.str_v, " cannot update");
      } else
         execerror("insert invoked on non-map", NULL);
      if (!(args[1].flags & NOTASSIGN))
         freeDSE(args+1);
      break;
      }
   case 2: {		/* void remove(map, ident) */
      /* args[0] is map, args[1] is ident */
      if (args[0].type != dMAP || args[1].type != dIDENT)
         execerror("incorrect data types in call to remove()", NULL);
      doremove(args+0, args[1].value.str_v);
      if (!(args[1].flags & NOTASSIGN))
         freeDSE(args+1);
      break;
      }
   case 3: {		/* void send(arg, ...) [max 20 args] */
      sendevent(mc, narg.value.int_v, args);
      break;
      }
   case 4: {		/* void append(window, window.dtype[, tstamp]); */
                        /* void append(sequence, arg[, ...]) */
/*
 * two arguments are required for ROWS limited windows
 * the tstamp third argument is required for SECS limited windows
 * for sequences, one can specify as many arguments as one wants
 */
      if (args[0].type != dWINDOW && args[0].type != dSEQUENCE)
         execerror("append: ", "only legal for windows and sequences");
      if (args[0].type == dWINDOW) {
         Window *w = args[0].value.win_v;
         if(w->wtype == dROWS && narg.value.int_v != 2)
            execerror("append: ", "ROW limited windows require 2 arguments");
         else if (w->wtype == dSECS && narg.value.int_v != 3)
            execerror("append: ", "SEC limited windows require 3 arguments");
         appendWindow(w, args+1, args+2);
      } else {
         Sequence *s = args[0].value.seq_v;
         appendSequence(s, nargs-1, args+1);
      }
      break;
      }
   case 5: {		/* void publish(topic, arg, ...) [max 20 args] */
      publishevent(mc, narg.value.int_v, args);
      break;
      }
   }
   mc->pc = mc->pc + 2;
}

enum ts_field { SEC_IN_MIN, MIN_IN_HOUR, HOUR_IN_DAY, DAY_IN_MONTH,
                MONTH_IN_YEAR, YEAR_IN, DAY_IN_WEEK};

static long long ts_field_value(enum ts_field field, tstamp_t ts) {
   struct tm result;
   time_t secs;
   unsigned long long nsecs;
   long long ans = 0;

   nsecs = ts / 1000000000;
   secs = nsecs;
   (void) localtime_r(&secs, &result);
   switch(field) {
      case SEC_IN_MIN:    ans = (long long)(result.tm_sec); break;
      case MIN_IN_HOUR:   ans = (long long)(result.tm_min); break;
      case HOUR_IN_DAY:   ans = (long long)(result.tm_hour); break;
      case DAY_IN_MONTH:  ans = (long long)(result.tm_mday); break;
      case MONTH_IN_YEAR: ans = (long long)(1 + result.tm_mon); break;
      case YEAR_IN:       ans = (long long)(1900 + result.tm_year); break;
      case DAY_IN_WEEK:   ans = (long long)(result.tm_wday); break;
   }
   return ans;
}

static Iterator *genIterator(DataStackEntry d) {
   Iterator it;
   int n;

   if (d.type == dMAP || d.type == dPTABLE) {
      char **keys;
      if (d.type == dMAP) {
         TSHTable *ht = d.value.map_v->ht;
         n = tsht_keys(ht, &keys);
         it.type = dMAP;
      } else {
         n = ptab_keys(d.value.str_v, &keys);
         it.type = dPTABLE;
      }
      if (n < 0)
         execerror("memory allocation failure generating iterator", NULL);
      it.u.m_idents = keys;
   } else {
      Window *w = d.value.win_v;
      LLIterator *iter = ll_iter_create(w->ll);
      if (! iter)
         execerror("memory allocation failure generating iterator", NULL);
      n = ll_length(w->ll);
      it.type = dWINDOW;
      it.dtype = w->dtype;
      it.u.w_it = iter;
   }
   it.next = 0;
   it.size = n;
   return iter_duplicate(it);
}

#define DEFAULT_SEQ_SIZE 10

static Sequence *genSequence(long long nargs, DataStackEntry *args) {
   Sequence s;
   DataStackEntry *q;
   int n = (nargs > 0) ? nargs : DEFAULT_SEQ_SIZE;
   q = (DataStackEntry *)malloc(n * sizeof(DataStackEntry));
   if (! q)
      execerror("allocation failure in Sequence()", NULL);
   s.size = n;
   s.used = 0;
   s.entries = q;
   appendSequence(&s, nargs, args);
   return seq_duplicate(s);
}

static int hasEntry(DataStackEntry *d, char *key) {
   if (d->type == dMAP) {
      Map *m = d->value.map_v;
      if (tsht_lookup(m->ht, key))
         return 1;
      else
         return 0;
   } else {
      return ptab_hasEntry(d->value.str_v, key);
   }
}

static int hasNext(Iterator *it) {
   return (it->next < it->size);
}

static DataStackEntry nextElement(Iterator *it) {
   DataStackEntry d;

   if (it->next >= it->size)
      execerror("next() invoked on exhausted iterator", NULL);
   if (it->type == dMAP || it->type == dPTABLE) {
      int n = it->next++;
      d.value.str_v = it->u.m_idents[n];
      d.type = dIDENT;
   } else {                        /* it's a window */
      WindowEntry *we = (WindowEntry *)ll_iter_next(it->u.w_it);
      d = we->dse;
   }
   d.flags = 0;
   return d;
}

static DataStackEntry seqElement(Sequence *seq, long long ndx) {
   if (ndx >= seq->used)
      execerror("seqElement(): index out of range", NULL);
   return (seq->entries[ndx]);
}

static long long IP4Addr(char *addr) {
   struct in_addr in;
   long long ipaddr;

   if (! inet_aton(addr, &in))
      execerror("Invalid IP address", addr);
   ipaddr = ntohl(in.s_addr);
   return (ipaddr & 0xffffffff);
}

static long long IP4Mask(long long slashN) {
   long long mask;
   int i;

   mask = 0;
   for (i = 0; i < slashN; i++)
      mask = (mask << 1) | 1;
   mask <<= (32 - slashN);
   return mask;
}

static int matchNetwork(char *str, long long mask, long long subnet) {
   long long addr = IP4Addr(str);
   return ((addr & mask) == subnet);
}

void function(MachineContext *mc) {
   DataStackEntry name = mc->pc->u.immediate;
   DataStackEntry narg = (mc->pc+1)->u.immediate;
   DataStackEntry args[MAX_ARGS], d;
   int i;
   unsigned int nargs;
   struct fpargs *range;

   nargs = narg.value.int_v;
   range = tsht_lookup(builtins, name.value.str_v);
   for (i = narg.value.int_v -1; i >= 0; i--)
      args[i] = pop(mc->stack);
   if (nargs < range->min || nargs > range->max)
      execerror("Incorrect # of arguments for function", name.value.str_v);
   if (iflog) {
      fprintf(stderr, "%08lx: %s(", au_id(mc->au), name.value.str_v);
      for (i = 0; i < narg.value.int_v; i++) {
         if (i > 0)
            fprintf(stderr, ", ");
         dumpDataStackEntry(args+i, 0);
      }
      fprintf(stderr, ")\n");
   }
   switch(range->index) {
   case 0: {		/* real float(int) */
      if (args[0].type != dINTEGER)
         execerror("argument to float must be an int", NULL);
      d.type = dDOUBLE;
      d.flags = 0;
      d.value.dbl_v = (double)(args[0].value.int_v);
      break;
      }
   case 1: {		/* identifier Identifier(arg, ...) [max 20 args] */
      d.type = dIDENT;
      d.flags = MUST_FREE;
      d.value.str_v = concat(dIDENT, narg.value.int_v, args);
      break;
      }
   case 2: {		/* map.type lookup(map, identifier) */
      /* args[0] is map, args[1] is ident to search for */
      if (args[0].type == dMAP) {
         lookup(args+0, args[1].value.str_v, &d);
         d.flags = 0;
      } else if (args[0].type == dPTABLE) {
         Sequence *s = ptab_lookup(args[0].value.str_v, args[1].value.str_v);
         if (s) {
            d.type = dSEQUENCE;
            d.flags = MUST_FREE;
            d.value.seq_v = s;
         } else
            execerror(args[1].value.str_v, " not mapped to a value");
      } else
         execerror("attempted lookup on non-map", NULL);
      if (!(args[1].flags & NOTASSIGN))
         freeDSE(args+1);
      break;
      }
   case 3: {		/* real average(window) */
      if (args[0].type != dWINDOW)
         execerror("attempt to compute average of a non-window", NULL);
      d.type = dDOUBLE;
      d.flags = 0;
      d.value.dbl_v = average(args[0].value.win_v);
      break;
      }
   case 4: {		/* real stdDev(window) */
      if (args[0].type != dWINDOW)
         execerror("attempt to compute std deviation of a non-window", NULL);
      d.type = dDOUBLE;
      d.flags = 0;
      d.value.dbl_v = std_dev(args[0].value.win_v);
      break;
      }
   case 5: {		/* string currentTopic() */
      d.type = dSTRING;
      d.flags = 0;
      d.value.str_v = mc->currentTopic;
      break;
      }
   case 6: {		/* iterator Iterator(map|win|seq) */
      if (args[0].type != dMAP && args[0].type != dPTABLE && args[0].type != dWINDOW)
         execerror("incorrectly typed argument to Iterator()", NULL);
      d.type = dITERATOR;
      d.flags = 0;
      d.value.iter_v = genIterator(args[0]);
      break;
      }
   case 7: {		/* identifier|data next(iterator) */
      if (args[0].type != dITERATOR)
         execerror("incorrectly typed argument to next()", NULL);
      d = nextElement(args[0].value.iter_v);
      break;
      }
   case 8: {		/* tstamp tstampNow() */
      d.type = dTSTAMP;
      d.flags = 0;
      d.value.tstamp_v = timestamp_now();
      break;
      }
   case 9: {		/* tstamp tstampDelta(tstamp, int, bool) */
      long units;
      int ifmillis;
      tstamp_t ts;
      if (args[0].type != dTSTAMP || args[1].type != dINTEGER
                                  || args[2].type != dBOOLEAN)
         execerror("incorrectly typed arguments to tstampDelta()", NULL);
      d.type = dTSTAMP;
      d.flags = 0;
      units = args[1].value.int_v;
      ifmillis = args[2].value.bool_v;
      ts = args[0].value.tstamp_v;
      if (units >= 0)
         d.value.tstamp_v = timestamp_add_incr(ts, units, ifmillis);
      else
         d.value.tstamp_v = timestamp_sub_incr(ts, -units, ifmillis);
      break;
      }
   case 10: {		/* int tstampDiff(tstamp, tstamp) */
      long long diff;
      if (args[0].type != dTSTAMP || args[1].type != dTSTAMP)
         execerror("incorrectly typed arguments to tstampDiff()", NULL);
      d.type = dINTEGER;
      d.flags = 0;
      if (args[0].value.tstamp_v >= args[1].value.tstamp_v)
         diff = args[0].value.tstamp_v - args[1].value.tstamp_v;
      else
         diff = -(args[1].value.tstamp_v - args[0].value.tstamp_v);
      d.value.int_v = diff;
      break;
      }
   case 11: {		/* tstamp Timestamp(string) */
      if (args[0].type != dSTRING)
         execerror("incorrectly typed argument to Timestamp()", NULL);
      d.type = dTSTAMP;
      d.flags = 0;
      d.value.tstamp_v = datestring_to_timestamp(args[0].value.str_v);
      if (!(args[0].flags & NOTASSIGN))
         freeDSE(&args[0]);
      break;
      }
   case 12: {		/* int dayInWeek(tstamp) [Sun is 0, Sat is 6] */
      if (args[0].type != dTSTAMP)
         execerror("incorrectly typed argument to dayInWeek()", NULL);
      d.type = dINTEGER;
      d.flags = 0;
      d.value.int_v = ts_field_value(DAY_IN_WEEK, args[0].value.tstamp_v);
      break;
      }
   case 13: {		/* int hourInDay(tstamp) [0 .. 23] */
      if (args[0].type != dTSTAMP)
         execerror("incorrectly typed argument to hourInDay()", NULL);
      d.type = dINTEGER;
      d.flags = 0;
      d.value.int_v = ts_field_value(HOUR_IN_DAY, args[0].value.tstamp_v);
      break;
      }
   case 14: {		/* int dayInMonth(tstamp) [1..31] */
      if (args[0].type != dTSTAMP)
         execerror("incorrectly typed argument to dayInMonth()", NULL);
      d.type = dINTEGER;
      d.flags = 0;
      d.value.int_v = ts_field_value(DAY_IN_MONTH, args[0].value.tstamp_v);
      break;
      }
   case 15: {		/* sequence Sequence() */
      d.type = dSEQUENCE;
      d.flags = 0;
      d.value.seq_v = genSequence(narg.value.int_v, args);
      break;
      }
   case 16: {		/* bool hasEntry(map, identifier) */
      if (! (args[1].type == dIDENT &&
             (args[0].type == dMAP || args[0].type == dPTABLE)))
         execerror("incorrectly typed arguments to hasEntry()", NULL);
      d.type = dBOOLEAN;
      d.flags = 0;
      d.value.bool_v = hasEntry(args, args[1].value.str_v);
      break;
      }
   case 17: {		/* bool hasNext(iterator) */
      if (args[0].type != dITERATOR)
         execerror("incorrectly typed argument to hasNext()", NULL);
      d.type = dBOOLEAN;
      d.flags = 0;
      d.value.bool_v = hasNext(args[0].value.iter_v);
      break;
      }
   case 18: {		/* string String(arg[, ...]) */
      d.type = dSTRING;
      d.flags = MUST_FREE;
      d.value.str_v = concat(dSTRING, narg.value.int_v, args);
      break;
      }
   case 19: {		/* basictype seqElement(seq, int) */
      if (args[0].type != dSEQUENCE || args[1].type != dINTEGER)
         execerror("incorrectly typed arguments to seqElement()", NULL);
      d = seqElement(args[0].value.seq_v, args[1].value.int_v);
      d.flags = 0;
      break;
      }
   case 20: {		/* int seqSize(seq) */
      if (args[0].type != dSEQUENCE)
         execerror("incorrectly typed argument to seqSize()", NULL);
      d.type = dINTEGER;
      d.flags = 0;
      d.value.int_v = args[0].value.seq_v->used;
      break;
      }
   case 21: {		/* int IP4Addr(string) */
      if (args[0].type != dSTRING)
         execerror("incorrectly typed argument to IP4Addr()", NULL);
      d.type = dINTEGER;
      d.flags = 0;
      d.value.int_v = IP4Addr(args[0].value.str_v);
      break;
      }
   case 22: {		/* int IP4Mask(int slashN) */
      if (args[0].type != dINTEGER)
         execerror("incorrectly typed argument to IP4Mask()", NULL);
      d.type = dINTEGER;
      d.flags = 0;
      d.value.int_v = IP4Mask(args[0].value.int_v);
      break;
      }
   case 23: {		/* bool matchNetwork(string, int, int) */
      if (args[0].type != dSTRING || args[1].type != dINTEGER
                                  || args[2].type != dINTEGER)
         execerror("incorrectly typed arguments to matchNetwork()", NULL);
      d.type = dBOOLEAN;
      d.flags = 0;
      d.value.bool_v = matchNetwork(args[0].value.str_v,
                                    args[1].value.int_v,
                                    args[2].value.int_v);
      break;
      }
   case 24: {		/* int secondInMinute(tstamp) [0 .. 60] */
      if (args[0].type != dTSTAMP)
         execerror("incorrectly typed argument to secondInMinute()", NULL);
      d.type = dINTEGER;
      d.flags = 0;
      d.value.int_v = ts_field_value(SEC_IN_MIN, args[0].value.tstamp_v);
      break;
      }
   case 25: {		/* int minuteInHour(tstamp) [0 .. 59] */
      if (args[0].type != dTSTAMP)
         execerror("incorrectly typed argument to minuteInHour()", NULL);
      d.type = dINTEGER;
      d.flags = 0;
      d.value.int_v = ts_field_value(MIN_IN_HOUR, args[0].value.tstamp_v);
      break;
      }
   case 26: {		/* int monthInYear(tstamp) [1 .. 12] */
      if (args[0].type != dTSTAMP)
         execerror("incorrectly typed argument to monthInYear()", NULL);
      d.type = dINTEGER;
      d.flags = 0;
      d.value.int_v = ts_field_value(MONTH_IN_YEAR, args[0].value.tstamp_v);
      break;
      }
   case 27: {		/* int yearIn(tstamp) [1900 .. ] */
      if (args[0].type != dTSTAMP)
         execerror("incorrectly typed argument to yearIn()", NULL);
      d.type = dINTEGER;
      d.flags = 0;
      d.value.int_v = ts_field_value(YEAR_IN, args[0].value.tstamp_v);
      break;
      }
   default: {		/* unknown function - should not get here */
      execerror(name.value.str_v, "unknown function");
      break;
      }
   }
   push(mc->stack, d);
   mc->pc = mc->pc + 2;
}

void dumpMap(DataStackEntry *d) {
   Map *m;
   if (d->type != dMAP) {
      fprintf(stderr, "dumpMap() asked to dump a nonMap");
      return;
   }
   m = d->value.map_v;
   fprintf(stderr, "map elements have type %d\n", m->type);
   fprintf(stderr, "keys: ");
   tsht_dump(m->ht, stderr);
}
