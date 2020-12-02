#include <stdio.h>
#include "automaton.h"
#include "topic.h"
#include <string.h>
#include <pthread.h>
#include "timestamp.h"
#include <stdlib.h>
#include <time.h>
#include <setjmp.h>
#include "code.h"
#include "ptable.h"

extern int iflog;
extern char *progname;
extern void a_init();
extern void ap_init();
extern int a_parse();

#define USAGE "./autoCompile -e env -a auto ..."

static int pack(char *file, char *program) {
   FILE *fp;

   if ((fp = fopen(file, "r")) != NULL) {
      int c;
      char *p = program;
      while ((c = fgetc(fp)) != EOF) {
         *p++ = c;
      }
      *p = '\0';
      fclose(fp);
      return 1;
   }
   return 0;
}

/*
 * expects `s' to be in the following format
 * 	[ \t]*[!`c']*`c'[ \t]*[!'\n']*
 *
 * skips leading blanks and tabs, copies first string up to `c' into `first'
 * skips `c', skips leading blanks and tabs, copies everything up to the
 * optional \n into `second'
 */
static void tokenize(char *s, int c, char *first, char *second) {
   char *p, *q;

   p = s;
   while (*p == ' ' || *p == '\t')	/* skip leading blanks and tabs */
      p++;
   q = first;
   while (*p != '\0') {			/* copy until detect c */
      if (*p == c || *p == '\n')
         break;
      *q++ = *p++;
   }
   *q = '\0';
   if (*p == c)
      p++;
   while (*p == ' ' || *p == '\t')	/* skip leading blanks and tabs */
      p++;
   q = second;
   while (*p != '\0') {			/* copy until detect \n */
      if (*p == '\n')
         break;
      *q++ = *p++;
   }
   *q = '\0';
}

static void process_environment(char *envf) {
   char bf[1024];
   char command[64];
   char rest[1024];
   char sname[64];
   char schema[1024];
   FILE *fs = fopen(envf, "r");
   if (! fs) {
      fprintf(stderr, "Error opening environment file: %s\n", envf);
      exit(-1);
   }
   while (fgets(bf, sizeof(bf), fs) != NULL) {
      if (!strchr(bf, ':')) {
         fprintf(stderr, "Illegal command in environment: %s", bf);
            continue;
      }
      tokenize(bf, ':', command, rest);
      if (strcmp(command, "Topic") == 0) {
         tokenize(rest, ' ', sname, schema);
         if (! top_create(sname, schema))
            fprintf(stderr, "create(%s, \"%s\") failed\n", sname, schema);
      } else if (strcmp(command, "PTable") == 0) {
         tokenize(rest, ' ', sname, schema);
         if (! top_create(sname, schema))
            fprintf(stderr, "create(%s, \"%s\") failed\n", sname, schema);
         ptab_create(sname);
      } else
         fprintf(stderr, "unknown command: %s", bf);
   }
   fclose(fs);
}

static int process_automaton(char *autom) {
   char automaton[10240];
   jmp_buf begin;

   pack(autom, automaton);
   if (! setjmp(begin)) {
      ap_init(automaton);
      initcode();
      return (! a_parse());
   }
   return 0;
}

int main(int argc, char **argv) {
   char *env = NULL;
   char *autos[20];
   int nautos = 0;
   int i, j;

   progname = argv[0];
   for (i = 1; i < argc; ) {
      if ((j = i + 1) == argc) {
         fprintf(stderr, "usage: %s\n", USAGE);
         exit(1);
      }
      if (strcmp(argv[i], "-e") == 0)
         env = argv[j];
      else if (strcmp(argv[i], "-a") == 0) {
         autos[nautos++] = argv[j];
      } else {
         fprintf(stderr, "Unknown flag: %s %s\n", argv[i], argv[j]);
      }
      i = j + 1;
   }
   if (! env) {
      fprintf(stderr, "Must provide an environment file\n");
      fprintf(stderr, "usage: %s\n", USAGE);
      return -1;
   } else if (! nautos) {
      fprintf(stderr, "Must provide at least one automaton\n");
      fprintf(stderr, "usage: %s\n", USAGE);
      return -1;
   }
   a_init();			/* initialize the parser system */
   top_init();			/* initialize the topic system */
   au_init();			/* initialize the automaton system */
   process_environment(env);
   for (i = 0; i < nautos; i++) {
      printf("Compiling %s ...", autos[i]);
      if (process_automaton(autos[i]))
         printf(" OK\n");
      else
         printf(" ERR\n");
   }
   return 0;
}
