/*
 * The system monitor displays events on standard output.
 */

#include "util.h"
#include "rtab.h"
#include "srpc.h"
#include "config.h"
#include "timestamp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <sys/time.h>

#include <signal.h>

#include <pthread.h>

#define USAGE "./sysmonitor [-h host] [-p port]"

typedef struct sys_data {
	char msg[80];
	tstamp_t tstamp;
} SysData;

SysData *sys_convert(Rtab *results) {
	SysData *ans;
	if (! results || results->mtype != 0)
		return NULL;
	if (results->nrows != 1)
		return NULL;
	if (!(ans = (SysData *)malloc(sizeof(SysData))))
		return NULL;
	char **columns;
	columns = rtab_getrow(results, 0);
	/* populate record */
	ans->tstamp = string_to_timestamp(columns[0]);
	memset(ans->msg, 0, sizeof(ans->msg));
	strncpy(ans->msg, columns[1], sizeof(ans->msg)-1);
	return ans;
}

void sys_free(SysData *p) {
	if (p) {
		free(p);
	}
}

static RpcService rps;

static int exit_status = 0;
static int sig_received;

static void *handler(void *args) {
	char event[SOCK_RECV_BUF_LEN], resp[100];
	char stsmsg[RTAB_MSG_MAX_LENGTH];
	RpcEndpoint sender;
	unsigned len, rlen;
	Rtab *results;
	SysData *sd;
	while ((len = rpc_query(rps, &sender, event, SOCK_RECV_BUF_LEN)) > 0) {
		sprintf(resp, "OK");
		rlen = strlen(resp) + 1;
		rpc_response(rps, &sender, resp, rlen);
		event[len] = '\0';
		results = rtab_unpack(event, len);
		/* rtab_print(results); */
		if (results && ! rtab_status(event, stsmsg)) {
			sd = sys_convert(results);
			if (sd) {
				char *s = timestamp_to_string(sd->tstamp);
				printf( "%s %s\n", s, sd->msg);
				free(s);
			}
			sys_free(sd);
		}
		rtab_free(results);
	}
	return (args) ? NULL : args; /* unused warning subterfuge */
}

static void signal_handler(int signum) {
	if (signum == SIGTERM) {
		sig_received = signum;
	}
}

static int parse_response(char *response, char *comment) {
	char *p, *q, *r;
	int n = 0;
	q = strstr(response, "<|>");
	for(p = response; p != q; p++) {
		n = 10 * n + (*p - '0');
	}
	p = q + 3;
	q = strstr(p, "<|>");
	r = comment;
	while (p != q) {
		*r++ = *p++;
	}
	*r = '\0';
	return n;
}

int main(int argc, char *argv[]) {
	RpcConnection rpc;
	unsigned rlen;
	Q_Decl(question,1000);
	char resp[100], myhost[100], qname[64];
	unsigned short myport;
	pthread_t thr;
	int i, j;
	unsigned short port;
	char *target;
	char *service;
	sigset_t mask, oldmask;
	
	char id[25]; /* ID returned by subscribe */
	int status;

	service = "SysMonitorHandler";
	target = HWDB_SERVER_ADDR;
	port = HWDB_SERVER_PORT;
	setlinebuf(stdout);
	for (i = 1; i < argc; ) {
		if ((j = i + 1) == argc) {
			fprintf(stderr, "usage: %s\n", USAGE);
			exit(1);
		}
		if (strcmp(argv[i], "-h") == 0)
			target = argv[j];
		else if (strcmp(argv[i], "-p") == 0)
			port = atoi(argv[j]);
		else {
			fprintf(stderr, "Unknown flag: %s %s\n", 
				argv[i], argv[j]);
		}
		i = j + 1;
	}
	/* initialize the RPC system and offer the Callback service */
	if (!rpc_init(0)) {
		fprintf(stderr, "Initialization failure for rpc system\n");
		exit(-1);
	}
	rps = rpc_offer(service);
	if (! rps) {
		fprintf(stderr, "Failure offering %s service\n", service);
		exit(-1);
	}
	rpc_details(myhost, &myport);
	
	/* connect to HWDB service */
	rpc = rpc_connect(target, port, "HWDB", 1l);
	if (rpc == NULL) {
		fprintf(stderr, "Error connecting to HWDB at %s:%05u\n", 
			target, port);
		exit(1);
	}
	sprintf(qname, "SysLast");
	/* subscribe to query 'qname' */
	sprintf(question, "SQL:subscribe %s %s %hu %s", 
		qname, myhost, myport, service);
	if (!rpc_call(rpc, Q_Arg(question), strlen(question)+1, resp, 100, &rlen)) {
		fprintf(stderr, "Error issuing subscribe command\n");
		exit(1);
	}
	resp[rlen] = '\0';
	status = parse_response(resp, id);
	if (status) {
		printf("subscribe %s", resp);
		exit(1);
	} else { /* Success */
		printf("subscribe 0<|>Success<|>0<|>0<|>\n");
	}

	/* start handler thread */
	if (pthread_create(&thr, NULL, handler, NULL)) {
		fprintf(stderr, "Failure to start handler thread\n");
		exit(-1);
	}
	/* establish signal handlers to gracefully exit from loop */
	if (signal(SIGTERM, signal_handler) == SIG_IGN)
		signal(SIGTERM, SIG_IGN);
	if (signal(SIGINT, signal_handler) == SIG_IGN)
	 	signal(SIGINT, SIG_IGN);
	if (signal(SIGHUP, signal_handler) == SIG_IGN)
		signal(SIGHUP, SIG_IGN);
	if (signal(SIGQUIT, signal_handler) == SIG_IGN)
		signal(SIGQUIT, SIG_IGN);
	sigemptyset(&mask);
	sigfillset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGQUIT);
	/* suspend until signal */
	sigprocmask(SIG_BLOCK, &mask, &oldmask);
	while (!sig_received)
		sigsuspend(&oldmask);
	sigprocmask(SIG_UNBLOCK, &mask, NULL);
	/* issue unsubscribe command */
	sprintf(question, "SQL:unsubscribe %s", id);
	if (!rpc_call(rpc, Q_Arg(question), strlen(question)+1, resp, 100, &rlen)) {
		fprintf(stderr, "Error issuing unsubscribe command\n");
		exit(1);
	}
	resp[rlen] = '\0';
	printf("unsubscribe %s", resp);
	/* disconnect from server */
	rpc_disconnect(rpc);
	exit(exit_status);
}

