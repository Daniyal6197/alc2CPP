/*
 * generic usage: *persist [-h host] [-p port] 
 * [-r rotation period] 
 * [-d datadir]
 * [-t table]
 * [-u select period]
 * [-a mysql]
 *
 * it periodically solicits all records for 'table' in each period
 * and writes the rtab records to a file
 */

#include "config.h"

#include "util.h"
#include "rtab.h"
#include "srpc.h"

#include "timestamp.h"

#include <string.h>
#include <stdlib.h>

#include <stdio.h>

#include <time.h>
#include <sys/time.h>

#include <signal.h>

#include <pthread.h>

#include <mysql/mysql.h>

#define USAGE "./persist [-h host] [-p port] [-r period (in hours)] [-d dir] [-t table]"

#define TIME_DELTA 5 /* in seconds */
#define AN_HOUR_IN_SECS 3600 /* an hour in seconds */

static int verbose = 0;

static struct timespec time_delay = {TIME_DELTA, 0};
static struct timespec a_rotation = {AN_HOUR_IN_SECS, 0};

static int must_exit = 0;
static int sig_received;
static int exit_status = 0;

static char database[256];

static char directory[64];
static char table[64];

static MYSQL mysql;

static int next_rotation; /* hours until next rotation */

static int update_period; /* seconds until next query */

/* Set to true in order to associate HWDB table with a MySQL table */
static int associate = 0;

const char* suffix = ".data";

static RpcConnection rpc, pdc;
static FILE *outf;

static pthread_t rotation_thr;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static tstamp_t ct; /* current time */

static void *handler(void *); /* the rotation thread */

static tstamp_t processresults(char *buf, unsigned int len);

static void signal_handler(int signum) {
	must_exit = 1;
	sig_received = signum;
}

int main(int argc, char *argv[]) {
	Q_Decl(query,SOCK_RECV_BUF_LEN);
	char  resp[SOCK_RECV_BUF_LEN];
	
	int qlen;
	unsigned len;
    
	char *host;
	unsigned short port;
	
	int i, j;
	struct timeval expected, current;
	
	tstamp_t last = 0LL;

	host = HWDB_SERVER_ADDR;
	port = HWDB_SERVER_PORT;
	
	next_rotation = 24;
	update_period = TIME_DELTA;
	
	my_bool reconnect = 1;
	unsigned int timeout = 31536000;
	
	table[0] = '\0';
    directory[0] = '\0';

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
		if (strcmp(argv[i], "-d") == 0)
			strcpy(directory, argv[j]);
		else 
		if (strcmp(argv[i], "-r") == 0)
			next_rotation = atoi(argv[j]);
		else 
		if (strcmp(argv[i], "-t") == 0)
			strcpy(table, argv[j]);
		else
		if (strcmp(argv[i], "-v") == 0)
			verbose = atoi(argv[j]);
		else
		if (strcmp(argv[i], "-u") == 0)
			update_period = atoi(argv[j]);
		else
		if (strcmp(argv[i], "-a") == 0) {
			if (strcmp(argv[j], "mysql") == 0)
				associate = 1; /* Insert into MySQL. */
		}
		else {
			fprintf(stderr, "Unknown flag: %s %s\n", argv[i], argv[j]);
		}
		i = j + 1;
	}
	if (table[0] == '\0' || directory[0] == '\0') {
		fprintf(stderr, "usage: %s\n", USAGE);
		exit(-1);
	}
	if (next_rotation < 1) {
	fprintf(stderr, "Error: try rotation period values (hours) in [1,24).\n");
	exit(-1);
	}
	
	if (associate) { /* Connect to MySQL */
		
		mysql_init(&mysql);
		mysql_options(&mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
		mysql_options(&mysql, MYSQL_OPT_RECONNECT, &reconnect);
		if (! mysql_real_connect(&mysql, host, "homeuser", "homework", 
			"Homework", 0, NULL, 0)) {
			/* Parameters are hostname, username, password, database, 
			port, socket, and flags. */
			fprintf(stderr, "mysql error: %s\n", 
				mysql_error(&mysql));
			exit(-1);
		}
	}

	/* Create and open database filename; and set next file rotation */
	memset(database, 0, sizeof(database));
	ct = timestamp_now();
	char *cts = timestamp_to_filestring(ct);
	sprintf(database, "%s/%s-%s%s", directory, table, cts, suffix);
	free(cts);
	if (verbose > 0) 
		printf("Writing data to %s\n", database);
	if ((outf = fopen(database, "a")) == NULL) {
		fprintf(stderr, "Error opening %s\n", database);
		exit(-1);
	}
	if (next_rotation == 24) 
		a_rotation.tv_sec = seconds_to_midnight(ct);
	else 
		/* a_rotation.tv_sec = (next_rotation) * AN_HOUR_IN_SECS; */
		a_rotation.tv_sec = next_rotation;
	if (verbose > 0) 
		printf("Next rotation in %d seconds\n", (int) a_rotation.tv_sec);
	/* Create worker thread (one performing the file rotation). */
	if (pthread_create(&rotation_thr, NULL, handler, NULL)) {
		fprintf(stderr, "Error creating rotation thread\n");
		exit(-1);
	}
	/* First attempt to connect to the database server. */
	if (!rpc_init(0)) {
		fprintf(stderr, "Error initializing rpc system\n");
		exit(-1);
	}
	if (!(rpc = rpc_connect(host, port, "HWDB", 1l))) {
		fprintf(stderr, "Error connecting to HWDB at %s:%05u\n", host, port);
		exit(-1);
	}
	
	/*
	if (!(pdc = rpc_connect(host, HWDB_PERSISTSERVER_PORT, "PDB", 1l))) {
		fprintf(stderr, "Error connecting to PDB at %s:%05u\n", 
			host, HWDB_PERSISTSERVER_PORT);
		pdc = NULL;
	}
	*/

	pdc = NULL;

	/* Now establish signal handlers to gracefully exit from loop. */
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
		expected.tv_sec += update_period;
		if (last) {
			char *s = timestamp_to_string(last);
			sprintf(query, "SQL:select * from %s [ since %s ]\n", table, s);
			free(s);
		} else
			sprintf(query, "SQL:select * from %s [ range %d seconds ]\n",
				table, update_period);
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
        if (! rpc_call(rpc, Q_Arg(query), qlen, resp, sizeof(resp), &len)) {
			fprintf(stderr, "rpc_call() failed\n");
			break;
		}
		resp[len] = '\0';
		if ((last_seen = processresults(resp, len)))
			last = last_seen;
    }
	printf("Left loop, closing database %s...\n", database);
	rpc_disconnect(rpc);
	if (outf != NULL)
		fclose(outf);
	exit(exit_status);
}

static tstamp_t processresults(char *buf, unsigned int len) {
	char *p;
	char t[1024]; /* A temporary buffer. */
	char q[1024]; /* The mysql query. */
	char u[1024]; /* Hm. The update statement. */
	int l, m;
	tstamp_t last = 0LL;
	int mtype, size, ncols, nrows;
	int i, j; /* Iterate over results. */
	
	my_ulonglong naffected;

	size = (int) len;
	size = (int) len;
	if (! rtab_status(buf, t)) {
		p = buf;
		p = rtab_fetch_int(p, &mtype);
		p = rtab_fetch_str(p, t, &size);
		p = rtab_fetch_int(p, &ncols);
		p = rtab_fetch_int(p, &nrows);
		if (nrows > 0) {
			pthread_mutex_lock(&mutex);
			p = strchr(buf, '\n') + 1; /* skip over line 1 */
			
			if (associate) { /* Create mysql query. */
				m = 0;
				m += sprintf(u + m, " on duplicate key update ");
				/* Skip timestamp. */
				p = strchr(p, ':');
				p++;
				for (i = 1; i < ncols; i++) {
					p = strchr(p, ':');
					p = rtab_fetch_str(p+1, t, &size);
					if (verbose > 0) 
						printf("v = %s size = %d\n", t, size);
					if (i == ncols - 1) /* last attribute */
						m += sprintf(u + m, "%s=values(%s)  ", t, t);
					else
						m += sprintf(u + m, "%s=values(%s), ", t, t);
				}
				if (verbose > 0)
					printf("MySQL: %s (%d)\n", u, m);
			}

			p = strchr(p, '\n') + 1; /* skip over line 2 */
			/* should now have nrows lines */
			if (verbose > 0) {
				printf("%s", p);
			}
			
			fputs(p, outf);
			fflush(outf);
			
			if (associate) { /* Create mysql query. */
				l = 0;
				l += sprintf(q + l, "insert into %s values ", table);
				/* Process each line. */ 
				for (j = 0; j < nrows; j++) {
					l += sprintf(q + l, "(");
					/* Skip timestamp. */
					p = rtab_fetch_str(p, t, &size);
					for (i = 1; i < ncols; i++) {
						p = rtab_fetch_str(p, t, &size);
						if (verbose > 0) 
							printf("v = %s size = %d\n", t, size);
						if (i == ncols - 1) /* last attribute */
							l += sprintf(q + l, "\"%s\")", t);
						else
							l += sprintf(q + l, "\"%s\",", t);
					}
					if (j == nrows - 1) /* last row */
						l += sprintf(q + l, " " );
					else
						l += sprintf(q + l, ", ");
				}
				l += sprintf(q + l, "%s", u);
					if (verbose > 0)
						printf("MySQL: %s (%d)\n", q, l);
				
				if (mysql_real_query(&mysql, q, strlen(q)) != 0) {
					fprintf(stderr, "mysql error: %s\n", 
						mysql_error(&mysql));
					exit(-1);
				}

				naffected = mysql_affected_rows(&mysql);
				if (verbose > 0)
					printf("%llu rows affected.\n", naffected);
				if (naffected == (my_ulonglong)(-1)) {
					fprintf(stderr, "mysql error: %s\n", 
						mysql_error(&mysql));
					exit(-1);
				}
			}

			pthread_mutex_unlock(&mutex);
			p = strrchr(buf, '\n'); /* last \n */
			while (*(--p) != '\n');	/* find penultimate \n */
			p = rtab_fetch_str(++p, t, &size);	/* timestamp */
			last = string_to_timestamp(t);
		}
	}
	return (last);
}

static void *handler(void *args) {
	
	Q_Decl(q,SOCK_RECV_BUF_LEN);
	char r[SOCK_RECV_BUF_LEN];
	unsigned l;

	for (;;) {
		if (nanosleep(&a_rotation, NULL) == -1)	/* signal woke us up */
			break;
		pthread_mutex_lock(&mutex);
		/* 1. Close existing database */
		fclose(outf);
		/* 2. Instruct 'persistserver' to load the current file */
		if (pdc) {
			sprintf(q, "LOAD:%s|%s\n", table, database);
			if (! rpc_call(pdc, Q_Arg(q), strlen(q) + 1, r, sizeof(r), &l)) {
            	fprintf(stderr, "rpc_call() failed\n");
        	} else {
				r[l] = '\0';
				if (verbose)
					printf("Response from persistserver: %s\n", r);
				if (strcmp(r, "OK") != 0) {
					printf("Failed to load file %s\n", database);
				}
			}
		}
		/* 3. Create new filename. */
		memset(database, 0, sizeof(database));
		ct = timestamp_now();
		char *cts = timestamp_to_filestring(ct);
		sprintf(database, "%s/%s-%s%s", directory, table, cts, suffix);
		free(cts);
		if (verbose > 0) 
			printf("New database file is %s\n", database);
		if (next_rotation == 24) 
			a_rotation.tv_sec = seconds_to_midnight(ct);
		else 
			/* a_rotation.tv_sec = (next_rotation) * AN_HOUR_IN_SECS; */
			a_rotation.tv_sec = (next_rotation);
		if (verbose > 0) 
			printf("Next rotation in %d seconds\n", (int) a_rotation.tv_sec);
		/* 4. Open the new database file in append mode. */
		outf = fopen(database, "a");
		pthread_mutex_unlock(&mutex);
		if (outf == NULL) {
			must_exit = 1;
			exit_status = -1;
			return NULL;
		}
	}
	return (args ? NULL : args);
}

