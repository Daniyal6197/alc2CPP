#include <mysql/mysql.h>

#include <stdio.h>

#include <string.h>

#include <stdlib.h>

#include "config.h"

#include "util.h"
#include "rtab.h"
#include "srpc.h"

static int verbose = 0;

#define USAGE "./reinstate [-h host] [-p port] [-t table~1] ... [-t table~n]"

typedef struct tbl_t *tblP; /* Table name. */
typedef struct tbl_t {
	tblP next;
	char *name;
} tbl_t;

static tblP table = NULL; /* A LIFO queue of tables and helper functions. */

static void table_free() {
	tblP p, q;
	for (p = table; p != NULL; p = q) {
		q = p->next;
		free(p->name);
		free(p);
	}
}

static int table_append(char *name) {
	tblP p;
	p = malloc(sizeof(tbl_t));
	if (! p) return 1;
	p->next = NULL;
	p->name = strdup(name);
	if (! table) {
		table = p;
	} else {
		p->next = table;
		table = p;
	}
	return 0;
}

static void reinstate(char *table);

static MYSQL mysql;
	
static RpcConnection rpc;
	
int main(int argc, char *argv[]) {
	
	char *host;
	unsigned short port;
    
	int i, j;
    
	host = HWDB_SERVER_ADDR;
	port = HWDB_SERVER_PORT;
	
	tblP t = NULL;
	
	for (i = 1; i < argc; ) {
		if ((j = i + 1) == argc) {
			fprintf(stderr, "usage: %s\n", USAGE);
			exit(-1);
		}
		if (strcmp(argv[i], "-h") == 0) host = argv[j];
		else 
		if (strcmp(argv[i], "-p") == 0) port = atoi(argv[j]);
		else 
		if (strcmp(argv[i], "-t") == 0) table_append(argv[j]);
		else {
			fprintf(stderr, "Unknown flag: %s %s\n", 
				argv[i], argv[j]);
		}
		i = j + 1;
	}
	
	if (! table) {
		fprintf(stderr, "No tables to reinstate.\n");
		exit(-1);
	}
	
	/* Connect to HWDB */
	if (!rpc_init(0)) {
		fprintf(stderr, "Failure to initialize rpc system.\n");
		table_free();
		exit(-1);
	}
	
	if (!(rpc = rpc_connect(host, port, "HWDB", 1l))) {
		fprintf(stderr, "Failure to connect to HWDB at %s:%05u.\n", 
			host, port);
		table_free();
		exit(-1);
	}
	
	/* Connect to MySQL */
	mysql_init(&mysql);

	if (! mysql_real_connect(&mysql, host, "homeuser", "homework", 
		"Homework", 0, NULL, 0)) {
		fprintf(stderr, "mysql error: %s.\n", mysql_error(&mysql));
		table_free();
		exit(-1);
	}
	
	for (t = table; t != NULL; t = t->next) {
		printf("Reinstate table %s.\n", t->name);
		reinstate(t->name);
	}

	table_free();
	
	/* Disconnect from hwdb */
	rpc_disconnect(rpc);
	
	/* Disconnect from MySQL */
	mysql_close(&mysql);

	exit(0);
}

#define QUERY_SIZE 32768 

static void reinstate(char *table) {
	
	MYSQL_RES *result;
	MYSQL_ROW row;
	MYSQL_FIELD *field;
	
	unsigned int nfields;
	unsigned long long nrows;

	char q[1024];
	
	char question[SOCK_RECV_BUF_LEN];
	char response[SOCK_RECV_BUF_LEN];
	
	unsigned long long count;

	Q_Decl(msg, SOCK_RECV_BUF_LEN);
	
	char stsmsg[RTAB_MSG_MAX_LENGTH];
	
	unsigned int l, bytes;
	unsigned length;

	unsigned int k;

	memset(q, 0, sizeof(q));
	sprintf(q, "SELECT * FROM %s", table);
	
	if (mysql_real_query(&mysql, q, strlen(q)) != 0) {
		fprintf(stderr, "mysql error: %s.\n", mysql_error(&mysql));
		return ;
	}
	
	result = mysql_store_result(&mysql);
	if (! result) {
		fprintf(stderr, "mysql error: result set is empty.\n");
		return ;
	}

	nfields = mysql_num_fields(result);
	/* Since we use 'mysql_store_result', we can count the rows. */
	nrows = mysql_num_rows(result);
	
	if (nrows == 0 || nfields == 0) {
		fprintf(stderr, "%llu rows %u fields; nothing to insert.\n",
			nrows, nfields);
		mysql_free_result(result);
		return ;
	}
	fprintf(stderr, "Inserting %llu rows.\n", nrows);
	
	memset(question, 0, SOCK_RECV_BUF_LEN);
	memset(response, 0, SOCK_RECV_BUF_LEN);
	
	memset(msg, 0, SOCK_RECV_BUF_LEN);
	bytes = 0;
	
	l = 0;
	count = 0;

	while ((row = mysql_fetch_row(result))) {

		// Reset to field 0.
		mysql_field_seek(result, 0);
		
		l += sprintf(question + l, "insert into %s values (", table);

		for (k = 0; k < nfields; k++) {
			
			// Get current field type.
			field = mysql_fetch_field(result);

			if (IS_NUM(field->type)) {
				if (field->type == MYSQL_TYPE_TINY) {
					if (strcmp(row[k], "1") == 0)
						l += sprintf(question + l,  "'true'");
					else
						l += sprintf(question + l, "'false'");
						
				} else {
					l += sprintf(question + l, "'%s'",   row[k]);
				}
			} else {
				l += sprintf(question + l, "\"%s\"", row[k]);
			}

			// Comma-separated values.
			if (k != (nfields - 1)) l += sprintf(question + l, ",");
		}
		l += sprintf(question + l, ")\n");

		/* One more row written in 'question'. */
		count += 1;
				
		if (l > QUERY_SIZE) {
			memset(msg, 0, sizeof(msg)); /* Reset message. */
			bytes = 0;
			bytes += sprintf(msg + bytes, "BULK:%llu\n", count);
			bytes += sprintf(msg + bytes, "%s", question);
			
			if (! rpc_call(rpc, Q_Arg(msg), bytes, response, sizeof(response), 
			&length)) {
				fprintf(stderr, "rpc_call() failed\n");
				return ;
			}
			
			response[length] = '\0';
			if (rtab_status(response, stsmsg))
				fprintf(stderr, "RPC error: %s\n", stsmsg);	
			
			if (verbose > 0) {
				fprintf(stderr, "A fragment of %llu rows (%u bytes) inserted.\n", 
				count, bytes);
			}

			l = 0;
			count = 0;
			memset(question, 0, sizeof(question)); /* Reset question. */
		}
	}

	mysql_free_result(result); /* Free mysql results. */
	
	/* Insert last fragment, if non-empty. */
	if (count > 0 && l > 0) {
		memset(msg, 0, sizeof(msg)); /* Reset message. */
		bytes = 0;
		bytes += sprintf(msg + bytes, "BULK:%llu\n", count);
		bytes += sprintf(msg + bytes, "%s", question);

		if (! rpc_call(rpc, Q_Arg(msg), bytes, response, sizeof(response), 
			&length)) {
			fprintf(stderr, "rpc_call() failed\n");	
			return ;
		}

		response[length] = '\0';
		if (rtab_status(response, stsmsg))
			fprintf(stderr, "RPC error: %s\n", stsmsg);
		
		if (verbose > 0) {
			fprintf(stderr, "Last fragment of %llu rows (%u bytes) inserted.\n", 
			count, bytes);
		}
	}
	return ;
}

