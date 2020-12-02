#ifndef _MACHINECONTEXT_H_
#define _MACHINECONTEXT_H_

/*
 * execution context for automaton virtual machine
 */

#include "dataStackEntry.h"
#include "tshtable.h"
#include "stack.h"
#include "automaton.h"

typedef struct instructionEntry InstructionEntry;

typedef struct machineContext {
   TSHTable *variables;
   Stack *stack;
   char *currentTopic;
   Automaton *au;
   InstructionEntry *pc;
} MachineContext;

/*
 * instructions for the stack machine are instances of the following union
 */

typedef void (*Inst)(MachineContext *mc);  /* actual machine instruction */
#define STOP (Inst)0

#define FUNC 1
#define DATA 2
#define PNTR 3

struct instructionEntry {
   int type;
   union {
      Inst op;
      DataStackEntry immediate;
      int offset;			/* offset from current PC */
   } u;
   char *label;
};

#endif /* _MACHINECONTEXT_H_ */
