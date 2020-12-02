#ifndef _PDB_H_
#define _PDB_H_

#include "rtab.h"

int  pdb_init(char *directory);
Rtab *pdb_exec_query(char *query);
void pdb_close();
int  pdb_load(char *cmd);

#endif /* _PDB_H_ */

