#ifndef _PTABLE_H_
#define _PTABLE_H_

#include "dataStackEntry.h"

int      ptab_exist(char *name);
int      ptab_create(char *name);
int      ptab_hasEntry(char *name, char *ident);
Sequence *ptab_lookup(char *name, char *ident);
int      ptab_update(char *name, char *ident, Sequence *value);
int      ptab_keys(char *name, char ***theKeys);

#endif /* _PTABLE_H_ */
