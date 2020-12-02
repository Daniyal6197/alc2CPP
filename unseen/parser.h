#ifndef _PARSER_H_
#define _PARSER_H_

#include "sqlstmts.h"
#include "y.tab.h"

/*
 * Places parsed output in externally declared global variable:
 *     sqlstmt stmt
 */
void *sql_parse(char *query);

void reset_statement(void);
void sql_reset_parser(void *bufstate);
void sql_dup_stmt(sqlstmt *dup);

/*
 * Prints externally declared global variable
 *     sqlstmt stmt
 * to standard output
 */
void sql_print();

#endif /* _PARSER_H_ */
