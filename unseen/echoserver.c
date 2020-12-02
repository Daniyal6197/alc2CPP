/*
 * Echo server
 *
 * single-threaded provider of the Echo service using SRPC
 *
 * legal queries and corresponding responses (all characters):
 *   ECHO:EOS-terminated-string --> 1/0
 *   SINK:EOS-terminated-string --> 1/0
 *   SGEN: --> 0/1EOS-terminated-string
 */

#include "srpc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define PORT 20000
#define SERVICE "Echo"
#define USAGE "./echoserver [-p port] [-s service]"

static const char letters[] = "abcdefghijklmnopqrstuvwxyz0123456789";

static void sgen(char *s) {
	int i, j, N;

	N = random() % 75 + 1;
	for (i = 0; i < N; i++) {
		j = random() % 36;
		*s++ = letters[j];
	}
	*s = '\0';
}

int main(int argc, char *argv[]) {
	RpcEndpoint sender;
	char *query = (char *)malloc(65536);
	char *resp = (char *)malloc(65536);
	char cmd[64], rest[65536];
	unsigned len;
	RpcService rps;
	char *service;
	unsigned short port;
	int i, j;

	service = SERVICE;
	port = PORT;
	for (i = 1; i < argc; ) {
		if ((j = i + 1) == argc) {
			fprintf(stderr, "usage: %s\n", USAGE);
			exit(1);
		}
		if (strcmp(argv[i], "-p") == 0)
			port = atoi(argv[j]);
		else if (strcmp(argv[i], "-s") == 0)
			service = argv[j];
		else {
			fprintf(stderr, "Unknown flag: %s %s\n", argv[i], argv[j]);
		}
		i = j + 1;
	}

	assert(rpc_init(port));
	rps = rpc_offer(service);
	if (! rps) {
		fprintf(stderr, "Failure offering Echo service\n");
		exit(-1);
	}
	while ((len = rpc_query(rps, &sender, query, 65536)) > 0) {
		int i;
		query[len] = '\0';
		rest[0] = '\0';
		for (i = 0; query[i] != '\0'; i++) {
			if (query[i] == ':') {
				strcpy(rest, &query[i+1]);
				break;
			} else
				cmd[i] = query[i];
		}
		cmd[i] = '\0';
		if (strcmp(cmd, "ECHO") == 0) {
			resp[0] = '1';
			strcpy(&resp[1], rest);
		} else if (strcmp(cmd, "SINK") == 0) {
			sprintf(resp, "1");
		} else if (strcmp(cmd, "SGEN") == 0) {
			resp[0] = '1';
			sgen(&resp[1]);
		} else {
			sprintf(resp, "0Illegal command %s", cmd);
		}
		rpc_response(rps, &sender, resp, strlen(resp) + 1);
	}
	return 0;
}
