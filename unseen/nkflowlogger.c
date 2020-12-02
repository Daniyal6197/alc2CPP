#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#include <arpa/inet.h> /* ntohs */

#include <signal.h>

#include "kernel/khwdb.h"

#include "kernel/debug.h"

#include "srpc.h"
#include "config.h"
#include "timestamp.h"

#define USAGE "./kflowlogger [-d device] [-t table]"

static RpcConnection rpc;
static char r[SOCK_RECV_BUF_LEN]; /* cf. config.h */

static int must_exit = 0;
static int sig_received;

static int f = 0;
unsigned long long count = 0LL;
	
static void signal_handler(int signum) {
	must_exit = 1;
	sig_received = signum;
	close(f);
	rpc_disconnect(rpc);
	dbg("kflowlogger exit [%llu]\n", count);
	exit(0);
}

static void process(char *s, int l, char *table) {
	
	unsigned int qlen;
	unsigned int rlen;

	unsigned long i;
	unsigned long elements = l/sizeof(struct __hwdb_flow);
	
	union __hwdb_data *data = (union __hwdb_data *) s;
	Q_Decl(q,SOCK_RECV_BUF_LEN);
	
	dbg("buffer size %d; %ld elements \n", l, elements);

	if (!s) /* unlikely */
		return;

	memset(q, 0, SOCK_RECV_BUF_LEN);
	memset(r, 0, SOCK_RECV_BUF_LEN);
	qlen = 0;
	rlen = 0;

	qlen += sprintf(q + qlen, "BULK:%ld\n", elements);
	for (i = 0; i < elements; i++) {
		
		tstamp_t t = timeval_to_timestamp(&data->flow[i].stamp);
		char *x = timestamp_to_string(t);
		
		qlen += sprintf(q + qlen, "insert into %s values ('%s', ", table, x);
		
		qlen += sprintf(q + qlen, "'%u', ", data->flow[i].key.protocol);
		
		qlen += sprintf(q + qlen, "\""IP4_FMT"\", '%hu', ",
			IP4_ARG(&data->flow[i].key.sa), ntohs(data->flow[i].key.sp));
		
		qlen += sprintf(q + qlen, "\""IP4_FMT"\", '%hu', ",
			IP4_ARG(&data->flow[i].key.da), ntohs(data->flow[i].key.dp));
		
		qlen += sprintf(q + qlen, "'%llu', '%llu', ",
			data->flow[i].packets, data->flow[i].bytes);
		
		qlen += sprintf(q + qlen, "'%u')\n", data->flow[i].flags);
		
		if (x) free(x);
	}
	dbg("kflowlogger[%u]>%s\n", qlen, q);
	if (! rpc_call(rpc, Q_Arg(q), qlen, r, sizeof(r), &rlen)) {
		fprintf(stderr, "rpc_call() failed\n");
	}
	dbg("hwdb[%u]>%s\n", rlen, r); 
}

int main(int argc, char *argv[]) {
	
	char buffer[HWDB_CBSIZE];
	int l;
	
	int i, j;
	char *target = HWDB_SERVER_ADDR;
	unsigned short port = HWDB_SERVER_PORT;
	char *table = "KFlows";
	char *device = "/dev/hwdb0";
	
	for (i = 1; i < argc; ) {
		if ((j = i + 1) == argc) {
			fprintf(stderr, "usage: %s\n", USAGE);
			exit(-1);
		}
		if (strcmp(argv[i], "-t") == 0)
			table = argv[j];
		else
		if (strcmp(argv[i], "-d") == 0)
			device = argv[j];
		else {
			fprintf(stderr, "Unknown flag: %s %s\n", argv[i], argv[j]);
		}
		i = j + 1;
	}
	
	f = open(device, O_RDONLY);
	if (f < 0) {
		fprintf(stderr, "Error opening %s\n", device);
		exit(-1);
	}
	
	if (! rpc_init(0)) {
		fprintf(stderr, "Error initializing rpc system\n");
		exit(-1);
	}
	rpc = rpc_connect(target, port, "HWDB", 1l);
	if (rpc == NULL) {
		fprintf(stderr, "Error connecting to HWDB at %s:%05u\n", target, port);
		exit(-1);
	}
	
	/* establish signal handlers */
	if (signal(SIGTERM, signal_handler) == SIG_IGN)
		signal(SIGTERM, SIG_IGN);
	if (signal(SIGINT, signal_handler) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
	if (signal(SIGHUP, signal_handler) == SIG_IGN)
		signal(SIGHUP, SIG_IGN);

	count = 0LL;
	dbg("kflowlogger reads\n");
	while (! must_exit) {
		memset(buffer, 0, sizeof(buffer));
		l = read(f, buffer, HWDB_CBSIZE);
		++count;
		process(buffer, l, table);
	}

	close(f);
	rpc_disconnect(rpc);
	dbg("kflowlogger exit [%llu]\n", count);
	exit(0);
}

