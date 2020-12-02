/* sample HTTP URL monitor */

#include "config.h"
#include "util.h"
#include "rtab.h"
#include "srpc.h"
#include "timestamp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netinet/in.h>

#define USAGE "./httpmonitor [-h host] [-p port]"
#define TIME_DELTA 10 /* in seconds */

static struct timespec time_delay = {TIME_DELTA, 0};
static int must_exit = 0;
static int exit_status = 0;
static int sig_received;

static tstamp_t processresults(char *buf, unsigned int len);

static void signal_handler(int signum) {
	must_exit = 1;
	sig_received = signum;
}

int main(int argc, char *argv[]) {
	RpcConnection rpc;
	Q_Decl(query,SOCK_RECV_BUF_LEN);
	char resp[SOCK_RECV_BUF_LEN];
	int qlen;
	unsigned len;
	char *host;
	unsigned short port;
	int i, j;
	struct timeval expected, current;
	tstamp_t last = 0LL;
	host = HWDB_SERVER_ADDR;
	port = HWDB_SERVER_PORT;
	for (i = 1; i < argc; ) {
		if ((j = i + 1) == argc) {
			fprintf(stderr, "usage: %s\n", USAGE);
			exit(1);
		}
		if (strcmp(argv[i], "-h") == 0)
			host = argv[j];
		else 
		if (strcmp(argv[i], "-p") == 0)
			port = atoi(argv[j]);
		else
			fprintf(stderr, "Unknown flag: %s %s\n", argv[i], argv[j]);
		i = j + 1;
	}
    
	if (!rpc_init(0)) {
		fprintf(stderr, "Failure to initialize rpc system\n");
		exit(-1);
	}
	if (!(rpc = rpc_connect(host, port, "HWDB", 1l))) {
		fprintf(stderr, "Failure to connect to HWDB at %s:%05u\n", host, port);
		exit(-1);
	}
    
	if (signal(SIGTERM, signal_handler) == SIG_IGN) signal(SIGTERM, SIG_IGN);
	if (signal(SIGINT,  signal_handler) == SIG_IGN) signal(SIGINT,  SIG_IGN);
	if (signal(SIGHUP,  signal_handler) == SIG_IGN) signal(SIGHUP,  SIG_IGN);

	gettimeofday(&expected, NULL);
	expected.tv_usec = 0;
	while (! must_exit) {
	tstamp_t last_seen;
	expected.tv_sec += TIME_DELTA;
	if (last) {
		char *s = timestamp_to_string(last);
		sprintf(query,
		"SQL:select * from Urls [ since %s ]\n", s);
		free(s);
	} else
		sprintf(query, "SQL:select * from Urls [ range %d seconds ]\n",
		TIME_DELTA);
	qlen = strlen(query) + 1;
	gettimeofday(&current, NULL);
	if (current.tv_usec > 0) {
		time_delay.tv_nsec = 1000 * (1000000 - current.tv_usec);
		time_delay.tv_sec = expected.tv_sec - current.tv_sec - 1;
	} else {
		time_delay.tv_nsec = 0;
		time_delay.tv_sec = expected.tv_sec - current.tv_sec;
	}
	nanosleep(&time_delay, NULL);
	fprintf(stderr, "%s", query);
	if (! rpc_call(rpc, Q_Arg(query), qlen, resp, sizeof(resp), &len)) {
		fprintf(stderr, "rpc_call() failed\n");	
		break;
	}
	resp[len] = '\0';
	if ((last_seen = processresults(resp, len)))
		last = last_seen;
	} /* end while */
	rpc_disconnect(rpc);
	exit(exit_status);
}

static tstamp_t processresults(char *buf, unsigned int len) {
	char *p;
	char tmpbuf[1024];
	tstamp_t last = 0LL;
	int mtype, size, ncols, nrows;
	size = (int)len;
	if (! rtab_status(buf, tmpbuf)) {
		p = buf;
		p = rtab_fetch_int(p, &mtype);
		p = rtab_fetch_str(p, tmpbuf, &size);
		p = rtab_fetch_int(p, &ncols);
		p = rtab_fetch_int(p, &nrows);
		printf("type %d size %d ncols %d nrows %d\n", 
		mtype, size, ncols, nrows);
		if (nrows > 0) {
			p = strchr(buf, '\n') + 1; /* skip over line 1 */
			p = strchr(p,   '\n') + 1; /* skip over line 2 */
			/* should now have nrows lines */
			fprintf(stderr, "%s", p);
			p = strrchr(buf, '\n'); /* last \n */
			while (*(--p) != '\n'); /* find penultimate \n */
			p = rtab_fetch_str(++p, tmpbuf, &size); /* timestamp */
			last = string_to_timestamp(tmpbuf);
		}
	}
	return (last);
}

