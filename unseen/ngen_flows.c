#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct flow {
   struct flow *next;
   struct flow *prev;
   int sdval;
   int ddval;
   int ticks;
   int port;
   int bytes;
} Flow;

static void gen_events(Flow *sentinel) {
   Flow *p;
   for (p = sentinel->next; p != sentinel; ) {
      printf("insert into Flows values ('17', \"130.209.247.%d\", '%d', \"10.2.0.%d\", '%d', '%d', '%d')\n", p->sdval, p->port, p->ddval, random() % 10000 + 1024, 1 + p->bytes/256, p->bytes);
      fflush(stdout);
      if (! (--p->ticks)) {
         Flow *q = p;
         p = q->next;
         q->prev->next = p;
         p->prev = q->prev;
         free(q);
      } else
         p = p->next;
   }
}

#define NSECS 5
#define UNUSED __attribute__ ((unused))

int main(UNUSED int argc, UNUSED char *argv[]) {
   Flow sentinel;
   int rate[4] = {10, 100, 1000, 10000};
   long nticks = 48 * 60 * 60 / NSECS;
   long scounter = 0;
   long dcounter = 0;

   sentinel.next = &sentinel;
   sentinel.prev = &sentinel;
   while (nticks-- > 0) {
      int i, nflows = (random() % 3);			/* 0 .. 2 */
      for (i = 0; i < nflows; i++) {
         int nticks = 5 + random() % (60 - 5 + 1);	/* 5 .. 60 */
         int sport = 5000 + scounter++ % 11;		/* 5000 .. 5010 */
         int nbytes = rate[random() % 4];
	 int sdval = 1 + random() % 254;
	 int ddval = 1 + dcounter++ % 20;
         Flow *f = (Flow *)malloc(sizeof(Flow));
         if (!f)
            break;
         f->ticks = nticks;
         f->port = sport;
         f->bytes = nbytes;
	 f->sdval = sdval;
	 f->ddval = ddval;
         f->next = &sentinel;
         (sentinel.prev)->next = f;
         f->prev = sentinel.prev;
         sentinel.prev = f;
      }
      gen_events(&sentinel);
      sleep(1);
   }
   return 0;
}
