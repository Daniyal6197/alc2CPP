#include "util.h"
#include "rtab.h"
#include "srpc.h"

#include "config.h"
#include "timestamp.h"

#include "flowmonitor.h"

#include <stdio.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <time.h>
#include <sys/time.h>

#include <arpa/inet.h>

#include <sys/types.h> 

#include <signal.h>

#define USAGE "./allowances [-h host] [-p port] [-s subnet]"

#define TIME_DELTA 5 /* in seconds */

#define _LEN_STR 80 /* string length */

static int dbg = 1;

static char subnet[_LEN_STR];

static struct timespec time_delay = {TIME_DELTA, 0};

static RpcConnection rpc;

static int exit_status = 0;

static int must_exit = 0;
static int sig_received = 0;

static void signal_handler(int signum) {
	must_exit = 1;
	sig_received = signum;
}

FlowResults *mon_convert(Rtab *results) {

	FlowResults *ans;
	unsigned int i;
	if (! results || results->mtype != 0)
		return NULL;
	if (!(ans = (FlowResults *) malloc(sizeof(FlowResults))))
		return NULL;
	ans->nflows = results->nrows;
	ans->data = (FlowData **) 
		calloc(ans->nflows, sizeof(FlowData *));
	if (! ans->data) {
		free(ans);
		return NULL;
	}
	for (i = 0; i < ans->nflows; i++) {
		char **columns;
		FlowData *p = (FlowData *) malloc(sizeof(FlowData));
		if (!p) {
			mon_free(ans);
			return NULL;
		}
		memset(p, 0, sizeof(FlowData));
		ans->data[i] = p;
		columns = rtab_getrow(results, i);
		
		p->tstamp = string_to_timestamp(columns[0]);
		inet_aton(columns[1], (struct in_addr *) &p->ip_dst);
		p->bytes = atol(columns[2]);
	}
	return ans;
}

void mon_free(FlowResults *p) {
	unsigned int i;
	if (p) {
		for (i = 0; i < p->nflows && p->data[i]; i++)
			free(p->data[i]);
		free(p);
	}
}

static tstamp_t processresults(char *buf, unsigned int len) {
	
	Rtab *results;
	char stsmsg[RTAB_MSG_MAX_LENGTH];
	FlowResults *p;
	unsigned long i = 0;

	Q_Decl(q,SOCK_RECV_BUF_LEN);
	char r[SOCK_RECV_BUF_LEN];
	unsigned rlen;

	int bytes;

	tstamp_t last = 0LL;
	results = rtab_unpack(buf, len);
	memset(stsmsg, 0, RTAB_MSG_MAX_LENGTH);
	if (results && ! rtab_status(buf, stsmsg)) {
	
	p = mon_convert(results);
	if (!p) {
		rtab_free(results);
		return(last);
	}
	if (dbg) fprintf(stderr, "retrieved %ld flow records\n", p->nflows);
	if (p->nflows == 0)
		goto out;
	bytes = 0;
	memset(q, 0, sizeof(q));
	bytes += sprintf(q + bytes, "BULK:%ld\n", p->nflows);
	for (i = 0; i < p->nflows; i++) {
	FlowData *f = p->data[i];
	char *ip = strdup(inet_ntoa(*(struct in_addr *)(&(f->ip_dst))));
	if (dbg) fprintf(stderr, "ip: %s\n", ip);
	/* update Allowances set bytes += X where ip = Y */
	bytes += sprintf(q + bytes, "update Allowances ");
	bytes += sprintf(q + bytes, "set nbytes+=%ld " , f->bytes);
	bytes += sprintf(q + bytes, "where ipaddr=\"%s\"\n" , ip);
	free(ip);
	if (dbg) fprintf(stderr, "%s[%d] [%u]\n", q, bytes, strlen(q) + 1);
	/* end for */
	}
	/* send query */
	if (! rpc_call(rpc, Q_Arg(q), strlen(q) +1, r, sizeof(r), &rlen)) {
		fprintf(stderr, "rpc_call() failed\n");
	}
	r[rlen] = '\0';
	if (dbg) fprintf(stderr, "%s\n", r);
out:
	if (i > 0) {
		i--;
		last = p->data[i]->tstamp;
	}
	mon_free(p);
	
	}
	rtab_free(results);
	return (last);
}

int main(int argc, char *argv[]) {

	int i, j;
	Q_Decl(q,SOCK_RECV_BUF_LEN);
	char r[SOCK_RECV_BUF_LEN];
	unsigned qlen;
	unsigned rlen;
	
	char *host = HWDB_SERVER_ADDR;
	unsigned short port = HWDB_SERVER_PORT;

	struct timeval expected, current;
	
	tstamp_t last = 0LL;
	
	subnet[0] = '\0';
	
	/* process arguments */
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
		strncpy(subnet, argv[j], _LEN_STR);
	else 
		fprintf(stderr, "Unknown flag: %s %s\n", 
		argv[i], argv[j]);
	i = j + 1;
	
	}
	if (subnet[0] == '\0') {
		fprintf(stderr, "usage: %s\n", USAGE);
		exit(-1);
	}
	if (dbg) fprintf(stderr, "subnet is %s\n", subnet);
	if (!rpc_init(0)) {
	
	fprintf(stderr, "Failure to initialize rpc system\n");
	exit(-1);
	
	}
	
	rpc = rpc_connect(host, port, "HWDB", 1l);
	if (! rpc) {
	
	fprintf(stderr, "Failure to connect to HWDB at %s:%05u\n", 
		host, port);
	exit(-1);
	
	}
	
	if (signal(SIGTERM, signal_handler) == SIG_IGN)
		signal(SIGTERM, SIG_IGN);
	if (signal(SIGINT , signal_handler) == SIG_IGN)
		signal(SIGINT , SIG_IGN);
	if (signal(SIGHUP , signal_handler) == SIG_IGN)
		signal(SIGHUP , SIG_IGN);

	gettimeofday(&expected, NULL);
	expected.tv_usec = 0;
	
	while (! must_exit) {
	
	tstamp_t last_seen;
	expected.tv_sec += TIME_DELTA;
	
	qlen = 0;
	qlen += sprintf(q, "SQL:select timestamp, daddr, sum(nbytes) ");
	qlen += sprintf(q + qlen, "from Flows ");
	if (last) {
		char *s = timestamp_to_string(last);
		qlen += sprintf(q + qlen, "[since %s ] ", s);
		free(s);
	} else {
		qlen += sprintf(q + qlen, "[range %d seconds] ", TIME_DELTA);
	}
	qlen += sprintf(q + qlen, "where ");
	qlen += sprintf(q + qlen, "daddr contains \"%s\" ", subnet);
	qlen += sprintf(q + qlen, "and ");
	qlen += sprintf(q + qlen, "saddr notcontains \"%s\" ", subnet);
	qlen += sprintf(q + qlen, "group by daddr order by timestamp\n");
	if (dbg) fprintf(stderr, "%s", q);
	gettimeofday(&current, NULL);
	if (current.tv_usec > 0) {
	
	time_delay.tv_nsec = 1000 * (1000000 - current.tv_usec);
	time_delay.tv_sec = expected.tv_sec - current.tv_sec - 1;
	
	} else {
	
	time_delay.tv_nsec = 0;
	time_delay.tv_sec = expected.tv_sec - current.tv_sec;
	
	}
	nanosleep(&time_delay, NULL);
	if (! rpc_call(rpc, Q_Arg(q), qlen, r, sizeof(r), &rlen)) {
	
	fprintf(stderr, "rpc_call() failed\n");
	exit_status = -1;
	break;
	
	}
	r[rlen] = '\0';
	if (dbg) fprintf(stderr, "%s\n", r);
	if ((last_seen = processresults(r, rlen)))
		last = last_seen;
	
	} /* end while loop */
	
	rpc_disconnect(rpc);
	printf("Bye.\n");
	exit(exit_status);
}

