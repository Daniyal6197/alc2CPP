/*
 * A client to terminate Homework Database services
 *
 * usage: ./sigterm [-s service] [-h host] [-p port]
 *
 */

#include "config.h"

#include "util.h"
#include "rtab.h"
#include "srpc.h"

#include <stdio.h>

#include <string.h>
#include <stdlib.h>

#define USAGE "./sigterm [-s service] [-h host] [-p port]"

int main(int argc, char *argv[]) {
	RpcConnection rpc;
	/* Command and response. */
	Q_Decl(c,SOCK_RECV_BUF_LEN);
	char r[SOCK_RECV_BUF_LEN];
	unsigned l;
	char *host;
	unsigned short port;
	char *service;
	int i, j;
	host = HWDB_SERVER_ADDR;
	port = HWDB_SERVER_PORT;
	service = NULL;
	for (i = 1; i < argc; ) {
		
		if ((j = i + 1) == argc) {
			fprintf(stderr, "usage: %s\n", USAGE);
			exit(-1);
		}
		if (strcmp(argv[i], "-h") == 0)
			host = argv[j];
		else
		if (strcmp(argv[i], "-p") == 0)
			port = atoi(argv[j]);
		else
		if (strcmp(argv[i], "-s") == 0)
			service = argv[j];
		else {
			fprintf(stderr, "Unknown flag: %s %s\n",
				argv[i], argv[j]);
		}
		i = j + 1;
	}
	if (! service) {
		fprintf(stderr, "usage: %s\n", USAGE);
		exit(-1);
	}
	if (! rpc_init(0)) {
		fprintf(stderr, "Failure to initialize rpc system\n");
		exit(-1);
	}
	if (!(rpc = rpc_connect(host, port, service, 1l))) {
		fprintf(stderr, "Failure to connect to %s at %s:%05u\n",
		service, host, port);
		exit(-1);
	}
	sprintf(c, "STOP:\n");
	if (! rpc_call(rpc, Q_Arg(c), strlen(c) + 1, r, sizeof(r), &l)) {
		fprintf(stderr, "rpc_call() failed\n");
		exit(-1);
	}
	r[l] = '\0';
	fprintf(stderr, "Response to sigterm command: %s", r);
	rpc_disconnect(rpc);
	exit(0);
}

