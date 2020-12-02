#include "config.h"
#include "util.h"
#include "rtab.h"
#include "srpc.h"
#include "timestamp.h"
#include "kflowmonitor.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netinet/in.h>

#define USAGE "./kflowmonitor [-h host] [-p port] [-t table]"
#define TIME_DELTA 5 /* in seconds */

static struct timespec time_delay = {TIME_DELTA, 0};
static int must_exit = 0;
static int exit_status = 0;
static int sig_received;

static char table[64];

static tstamp_t processresults(char *buf, unsigned int len);

static void signal_handler(int signum) {
	must_exit = 1;
	sig_received = signum;
}

int main(int argc, char *argv[]) {
	RpcConnection rpc;
	Q_Decl(q,SOCK_RECV_BUF_LEN);
	char r[SOCK_RECV_BUF_LEN];
	int qlen;
	unsigned rlen;
	char *host;
	unsigned short port;
	int i, j;
	struct timeval expected, current;
	tstamp_t last = 0LL;
	host = HWDB_SERVER_ADDR;
	port = HWDB_SERVER_PORT;
	table[0] = '\0';
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
		if (strcmp(argv[i], "-t") == 0)
			strcpy(table, argv[j]);
		else
			fprintf(stderr, "Unknown flag: %s %s\n", argv[i], argv[j]);
		i = j + 1;
	}
	if (table[0] == '\0') {
		fprintf(stderr, "usage: %s\n", USAGE);
		exit(-1);
	}
	/* first attempt to connect to the database server */
	if (!rpc_init(0)) {
		fprintf(stderr, "Failure to initialize rpc system\n");
		exit(-1);
	}
	if (!(rpc = rpc_connect(host, port, "HWDB", 1l))) {
		fprintf(stderr, "Failure to connect to HWDB at %s:%05u\n", host, port);
		exit(-1);
	}
	/* now establish signal handlers to gracefully exit from loop */
	if (signal(SIGTERM, signal_handler) == SIG_IGN)
		signal(SIGTERM, SIG_IGN);
	if (signal(SIGINT, signal_handler) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
	if (signal(SIGHUP, signal_handler) == SIG_IGN)
		signal(SIGHUP, SIG_IGN);
	gettimeofday(&expected, NULL);
	expected.tv_usec = 0;
	while (! must_exit) {
		tstamp_t last_seen;
		expected.tv_sec += TIME_DELTA;
		if (last) {
			char *s = timestamp_to_string(last);
			sprintf(q, "SQL:select * from %s [ since %s ] order by t\n", 
				table, s);
			free(s);
		} else
			sprintf(q, "SQL:select * from %s [ range %d seconds ] order by t\n",
				table, TIME_DELTA);
		qlen = strlen(q) + 1;
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
			break;
		}
		r[rlen] = '\0';
		if ((last_seen = processresults(r, rlen)))
			last = last_seen;
	}
	rpc_disconnect(rpc);
	exit(exit_status);
}

KFlows *mon_convert(Rtab *results) {
	KFlows *ans;
	unsigned int i;
	if (! results || results->mtype != 0)
		return NULL;
	if (!(ans = (KFlows *)malloc(sizeof(KFlows))))
		return NULL;
	ans->nflows = results->nrows;
	ans->data = (KFlow **)calloc(ans->nflows, sizeof(KFlow *));
	if (! ans->data) {
		free(ans);
		return NULL;
	}
	for (i = 0; i < ans->nflows; i++) {
		char **columns;
		KFlow *p = (KFlow *)malloc(sizeof(KFlow));
		if (!p) {
			mon_free(ans);
			return NULL;
		}
		ans->data[i] = p;
		columns = rtab_getrow(results, i);
		/* hwdb's timestamp */
		p->tstamp = string_to_timestamp(columns[0]);
		/* logger's timestamp */
		p->t = string_to_timestamp(columns[1]);
		/* flow's key */
		p->proto = atoi(columns[2]) & 0xff;
		inet_aton(columns[3], (struct in_addr *)&p->ip_src);
		p->sport = atoi(columns[4]) & 0xffff;
		inet_aton(columns[5], (struct in_addr *)&p->ip_dst);
		p->dport = atoi(columns[6]) & 0xffff;
		/* flow's stats */
		p->packets = atol(columns[7]);
		p->bytes = atol(columns[8]);
		p->flags = atoi(columns[9]) & 0xff;
	}
	return ans;
}

void mon_free(KFlows *p) {
	unsigned int i;
	if (p) {
		for (i = 0; i < p->nflows && p->data[i]; i++)
			free(p->data[i]);
		free(p->data);
		free(p);
	}
}

static tstamp_t processresults(char *buf, unsigned int len) {
	Rtab *results;
	char stsmsg[RTAB_MSG_MAX_LENGTH];
	KFlows *p;
	unsigned long i;
	tstamp_t last = 0LL;

	results = rtab_unpack(buf, len);
	if (results && ! rtab_status(buf, stsmsg)) {
		p = mon_convert(results);
		/* do something with the data pointed to by p */
		printf("Retrieved %ld flow records from database\n", p->nflows);
		for (i = 0; i < p->nflows; i++) {
			KFlow *f = p->data[i];
			char *hts = timestamp_to_string(f->tstamp);
			char *lts = timestamp_to_string(f->t);
			char *src = strdup(inet_ntoa(*(struct in_addr *)(&(f->ip_src))));
			char *dst = strdup(inet_ntoa(*(struct in_addr *)(&(f->ip_dst))));
			printf("%s %s:%u:%s:%s:%hu:%hu:%lu:%lu"FLG_FMT"\n", 
			hts, 
			lts,
			f->proto,
			src, 
			dst,
			f->sport, 
			f->dport,
			f->packets,
			f->bytes,
			FLG_ARG(f->flags)
			);
			free(hts);
			free(lts);
			free(src);
			free(dst);
		}
		if (i > 0) {
			i--;
			last = p->data[i]->tstamp;
		}
		mon_free(p);
	}
	rtab_free(results);
	return (last);
}

