#include <stdio.h>

#include <stdlib.h>

#include "dir.h"
#include "map.h"

#include "pdb.h"

#include "sqlstmts.h"
#include "parser.h"

#include "util.h"

static Map *map;

sqlstmt stmt;

static Rtab *pdb_exec_stmt();

static Rtab *pdb_select(sqlselect *select);

static int pdb_create(sqlcreate *create);

void pdb_close() {
	map_destroy(map);
	return;
}

int pdb_init(char *directory) {
	debugf("[pdb_init]\n");
	map = map_new(directory);
	if (!map) return 0;
	return 1;
}

Rtab *pdb_exec_query(char *query) {
	void *result;
	debugf("[pdb_exec_query]\n");
	result = sql_parse(query);
#ifdef VDEBUG
	sql_print();
#endif
	if (! result)
		return rtab_new_msg(RTAB_MSG_ERROR, NULL);
	return pdb_exec_stmt();
}

static Rtab *pdb_exec_stmt() {
	
	Rtab *results = NULL;
	
	switch (stmt.type) {
	
	case SQL_TYPE_SELECT:
	
	results = pdb_select(&stmt.sql.select);
	if (!results)
		results = rtab_new_msg(RTAB_MSG_SELECT_FAILED, NULL);
	break;
        
	case SQL_TYPE_CREATE:
	
	if (!pdb_create(&stmt.sql.create)) {
		results = rtab_new_msg(RTAB_MSG_CREATE_FAILED, NULL);
	} else {
		results = rtab_new_msg(RTAB_MSG_SUCCESS, NULL);
	}
	break;

	default:
		
	errorf("Error parsing query\n");
	results = rtab_new_msg(RTAB_MSG_PARSING_FAILED, NULL);
	break;
	
	}
	
	/*  query executed; clean-up */
	reset_statement();

	return results;
}

static Rtab *pdb_select(sqlselect *select) {
	Rtab *results;
	char *tablename;
	tablename = select->tables[0];
	debugf("select table %s\n", tablename);
	results = map_build_results(map, tablename, select);
	return results;
}

static int pdb_create(sqlcreate *create) {
	return map_create_table(map, create->tablename, create->ncols,
		create->colname, create->coltype);
}

int pdb_load(char *cmd) {
	return map_load(map, cmd);
}

