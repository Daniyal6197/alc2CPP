#ifndef _A_GLOBALS_H_
#define _A_GLOBALS_H_

/*
 * externs and struct definitions shared between the parser, the
 * stack machine and the automaton class
 */

#include "tshtable.h"
#include "machineContext.h"
#include <pthread.h>

#define MAX_ARGS 20

struct fpargs {
   unsigned int min;	/* minimum number of arguments for builtin */
   unsigned int max;    /* maximum number of arguments for builtin */
   unsigned int index;	/* index used for switch statements */
};

/* declared in agram.y */
extern TSHTable *variables;
extern TSHTable *topics;
extern TSHTable *builtins;
extern char *progname;
/* declared in code.c */
extern InstructionEntry *progp, *startp, *initialization, *behavior;
extern int initSize, behavSize;
extern int iflog;
/* declared in automaton.c */
extern pthread_key_t jmpbuf_key;
extern pthread_key_t execerr_key;

#endif /* _A_GLOBALS_H_ */
