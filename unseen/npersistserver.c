/*
 * Persistent database server
 */

#include <stdio.h>

#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <limits.h>

#include <pthread.h>

#include "pdb.h"

#include "rtab.h"

#include "config.h"

#include "srpc.h"

#define USAGE "./persistserver [-p port] [-c configuration file]"
#define ILLEGAL_QUERY_RESPONSE "1<|>Illegal query<|>0<|>0<|>\n"

static char b[SOCK_RECV_BUF_LEN];
static char r[SOCK_RECV_BUF_LEN];

static int log = 0;

static void loadfile(char *filename, int log) {
	
	FILE *f;
	int len;
	Rtab *results;
	char msg[RTAB_MSG_MAX_LENGTH];
	
	if (!(f = fopen(filename, "r"))) {
		fprintf(stderr, "Failed to open %s\n", filename);
		return;
	}

	while (fgets(b, sizeof(b), f) != NULL) {
		len = strlen(b) - 1; /* get rid of \n */
		if (len == 0)
			continue; /* ignore empty lines */
		if (b[0] == '#') {
			continue; /* ignore comments */
		}
		b[len] = '\0';
		if (log) printf("Q: %s\n", b);
		
		results = pdb_exec_query(b);
		if (! results)
			strcpy(r, ILLEGAL_QUERY_RESPONSE);
		else (void) rtab_pack(results, r, SOCK_RECV_BUF_LEN, &len);

		if (rtab_status(r, msg))
			fprintf(stderr, "Error: %s\n", msg);
		else if (log)
			rtab_print(results);
		rtab_free(results);
	}
	fclose(f);
	return;
}

int main(int argc, char *argv[]) {
	
	RpcService rps;
	RpcEndpoint sender;
	unsigned len;
	
	int i, j;
	Rtab *results;
	char *p, *q;

	unsigned short port;
	char *file;
	char *directory;
	
	port = HWDB_PERSISTSERVER_PORT;
	file = NULL;
	directory = "data";

	for (i = 1; i < argc; ) {
		if ((j = i + 1) == argc) {
			fprintf(stderr, "usage: %s\n", USAGE);
			exit(1);
		}
		if (strcmp(argv[i], "-p") == 0) {
			port = atoi(argv[j]);
		} else 
		if (strcmp(argv[i], "-c") == 0) {
			file = argv[j];
		} else 
		if (strcmp(argv[i], "-d") == 0) {
			directory = argv[j];
		} else {
			fprintf(stderr, "Unknown flag: %s %s\n", 
				argv[i], argv[j]);
		}
		i = j + 1;
	}

	/* init persistent database */
	pdb_init(directory);

	if (file) {
		/* process configuration file */
		loadfile(file, log);
	}

	if (! rpc_init(port)) {
		fprintf(stderr, "Failed to initialize rpc system\n");
		exit(-1);
	}

	rps = rpc_offer("PDB");
	if (! rps) {
		fprintf(stderr, "Failed to offer service\n");
		exit(-1);
	}
	
	/* start reading queries */
	printf("Waiting for queries on port %d.\n", port);
	while ((len = rpc_query(rps, &sender, b, SOCK_RECV_BUF_LEN)) > 0) {

		b[len] = '\0';
		if (log) printf("Q: %s\n", b);
		p = strchr(b, ':');
		if (p == NULL) {
			fprintf(stderr, "Illegal query: %s\n", b);
			strcpy(r, ILLEGAL_QUERY_RESPONSE);
			len = strlen(r) + 1;
			rpc_response(rps, &sender, r, len);
			continue;
		}
		*p++ = '\0';
		if (strcmp(b, "SQL") == 0) {
			q = p;
			p = strchr(q, '\n');
			if (p) *p++ ='\0';
			results = pdb_exec_query(q);
			if (log) {
				rtab_print(results);
			}
			if (! results) {
			
			strcpy(r, "1<|>Error<|>0<|>0<|>\n");
			len = strlen(r) + 1;
			
			} else {
			
			if (! rtab_pack(results, r, SOCK_RECV_BUF_LEN, &i))
				fprintf(stderr, "Query results truncated.\n");
			len = i;
			
			}
			rtab_free(results);

		} else if (strcmp(b, "STOP") == 0) {
			
			strcpy(r, "OK");
			len = strlen(r) + 1;
			rpc_response(rps, &sender, r, len);
			break;

		} else if (strcmp(b, "LOAD") == 0) {
			q = p;
			p = strchr(q, '\n');
			if (p) *p++ ='\0';
			printf("Load command %s\n", q);
			if (! pdb_load(q)) strcpy(r, "FAULT");
			else strcpy(r, "OK");
			len = strlen(r) + 1;
		} else {
			printf("Illegal query: %s:%s\n", b, p);
			strcpy(r, ILLEGAL_QUERY_RESPONSE);
			len = strlen(r) + 1;
		}
		rpc_response(rps, &sender, r, len);
	}

	pdb_close();
	printf("Bye.\n");
	return 0;
}

