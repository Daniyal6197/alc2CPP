#include <stdio.h>

#include <stdlib.h>

#include "pdb.h"

#include "rtab.h"

int main(void) {
	char *query;
	Rtab *results;
	pdb_init();
	query = "create table Test (proto integer, saddr varchar(16), sport integer, daddr varchar(16), dport integer, npkts integer, nbytes integer)";
	results = pdb_exec_query(query);
	rtab_free(results);
	query = "select * from Test";
	results = pdb_exec_query(query);
	rtab_print(results);
	rtab_free(results);
	pdb_close();
	exit(0);
}

