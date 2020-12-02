#ifndef _CODE_H_
#define _CODE_H_

/*
 * externs and instructions shared by the parser and the stack machine
 * interpreter
 */

#include "machineContext.h"
#include <stdio.h>

void             initcode(void);
void             switchcode(void);
void             endcode(void);
void             initstack(void);
void             execute(MachineContext *mc, InstructionEntry *i);
void             constpush(MachineContext *mc);
void             varpush(MachineContext *mc);
void             add(MachineContext *mc);
void             subtract(MachineContext *mc);
void             multiply(MachineContext *mc);
void             divide(MachineContext *mc);
void             modulo(MachineContext *mc);
void             negate(MachineContext *mc);
void             eval(MachineContext *mc);
void             extract(MachineContext *mc);
void             gt(MachineContext *mc);
void             ge(MachineContext *mc);
void             lt(MachineContext *mc);
void             le(MachineContext *mc);
void             eq(MachineContext *mc);
void             ne(MachineContext *mc);
void             and(MachineContext *mc);
void             or(MachineContext *mc);
void             not(MachineContext *mc);
void             print(MachineContext *mc);
void             function(MachineContext *mc);
void             print(MachineContext *mc);
void             assign(MachineContext *mc);
void             whilecode(MachineContext *mc);
void             ifcode(MachineContext *mc);
void             procedure(MachineContext *mc);
void             newmap(MachineContext *mc);
void             newwindow(MachineContext *mc);
void             destroy(MachineContext *mc);
void             bitOr(MachineContext *mc);
void             bitAnd(MachineContext *mc);
InstructionEntry *code(int ifInst, Inst f, DataStackEntry *d, char *what);
DataStackEntry   *dse_duplicate(DataStackEntry d);
Map              *map_duplicate(Map m);
Window           *win_duplicate(Window w);
WindowEntry      *we_duplicate(WindowEntry w);
void             warning(char *s1, char *s2);
void             execerror(char *s1, char *s2);
void             dumpMap(DataStackEntry *d);
void             dumpProgramBlock(InstructionEntry *i, int size);
void             dumpDataStackEntry (DataStackEntry *d, int verbose);
void             printDSE(DataStackEntry *d, FILE *fd);
void             freeDSE(DataStackEntry *d);
void             dumpCompilationResults(unsigned long id, TSHTable *v,
                                        InstructionEntry *init, int initSize,
                                        InstructionEntry *behav,
                                        int behavSize);

extern InstructionEntry *initialization, *behavior;

#endif /* _CODE_H_ */
