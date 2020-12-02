/*
 * Homework DB server
 *
 * single-threaded provider of the Homework Database using SRPC
 * just has the logic to start the database, obtaining queries from stdin
 * and printing results on stdout
 *
 * expects SQL statement in input buffer, sends back results of
 * query in output buffer
 */

#include "config.h"
#include "util.h"
#include "hwdb.h"
#include "rtab.h"
#include "mb.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define USAGE "./stdinserver [-l packets] [-f file]"

static char b[SOCK_RECV_BUF_LEN];
static char r[SOCK_RECV_BUF_LEN];

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
		b[len] = '\0';
		if (log) printf("Q: %s\n", b);
		
		results = hwdb_exec_query(b, 0);
		if (! results)
			sprintf(r, "1<|>Error<|>0<|>0<|>\n");
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

extern int log_allocation;

int main(int argc, char *argv[]) {
	unsigned len;
	int i, j;
	Rtab *results;
	int log;
	int isreadonly = 0;
	char *file = NULL;

	log = 0;
	for (i = 1; i < argc; ) {
		if ((j = i + 1) == argc) {
			fprintf(stderr, "usage: %s\n", USAGE);
			exit(1);
		}
		if (strcmp(argv[i], "-l") == 0) {
			if (strcmp(argv[j], "packets") == 0)
				log++;
			else
				fprintf(stderr, "usage: %s\n", USAGE);
		} else if (strcmp(argv[i], "-f") == 0) {
			file = argv[j];
		} else {
			fprintf(stderr, "Unknown flag: %s %s\n", argv[i], argv[j]);
		}
		i = j + 1;
	}
	printf("initializing database\n");
	hwdb_init(0);
	if (file) {
		loadfile(file, log);
	} else {
		printf("starting to read queries from stdin\n");
		/* log_allocation = 1; */
		j = 0;
		while (fgets(b, sizeof(b), stdin) != NULL) {
			len = strlen(b) - 1; /* get rid of \n */
			if (len == 0)
				continue; /* ignore empty lines */
			b[len] = '\0';
			j++;
			if (log)
				printf(">> %s\n", b);
			results = hwdb_exec_query(b, isreadonly);
			if (! results) {
			    sprintf(b, "1<|>Error<|>0<|>0<|>\n");
			    len = strlen(b) + 1;
			} else {
			    if (! rtab_pack(results, b, SOCK_RECV_BUF_LEN, &i)) {
				    printf("query results truncated\n");
			    }
			    len = i;
			}
			if (log)
				printf("<< %s", b);
			rtab_free(results);
			if (j >= 10000) {
				mb_dump();
				j = 0;
			}
		}
	}
	return 0;
}

