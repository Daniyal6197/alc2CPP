#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <ctype.h> /* isprint */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#include <arpa/inet.h> /* ntohs */

#include <signal.h>

#include "kernel/khwdb.h"

#include "kernel/debug.h"

#include "mem.h"
#include "srpc.h"
#include "config.h"
#include "timestamp.h"

#define USAGE "./khttplogger [-d device] [-t table]"

#define MAX_LENGTH 128

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
	dbg("khttplogger exit [%llu]\n", count);
	exit(0);
}

static char *readline(char *payload, unsigned int *offset) {
	char *prefix, *p;
	char *data = payload + *offset;
	char *eol = strstr(data, "\r\n"); /* crlf signifies end of line */
	if (!eol)
		return NULL;
	size_t linelength = eol - data;
	
	dbg("Line length %u\n", linelength);
	if (!linelength)
		return NULL;

	*offset += linelength + 2; /* set next offset */
	
	prefix = (char *) malloc(linelength + 1); /* Add '\0' */
	if (!prefix)
		return NULL;

	memset(prefix, '\0', linelength + 1);
	p = prefix;
	while (*data && linelength > 0) {
		if (isprint((int)*data)) {
			*prefix++ = *data++;
			linelength--;
		} else
			break;
	}
	prefix[linelength]='\0';
	dbg("prefix %s\n", p);
	return p;
}

static int parse_http_header(char *payload, unsigned int l,
	char *u, char *h, char *c) {

	char *URI, *Host, *Content_Type;
	
	size_t pos, len;

	unsigned int offset = 0;

	char *request_line = readline(payload, &offset);
	if (!request_line)
		return(-1);
	
	pos = 0;
	pos = strcspn(request_line + pos + 0, " "); /* 1st space */
	len = strcspn(request_line + pos + 1, " "); /* 2nd space */
	if (!len) {
		free(request_line);
		return(-1);
	}
	
	URI = malloc(len + 1);
	if (!URI) {
		free(request_line);
		return(-1); /* out of memory */
	}

	memset(URI, 0, len + 1);
	strncpy(URI, request_line + pos + 1, len);
	URI[len]='\0';
	dbg("URI %s\n", URI);
	strncpy(u, URI, MAX_LENGTH);
	free(URI);
	
	free(request_line);

	while (offset < l) {
		
		char *line = readline(payload, &offset);
		if (!line)
			return(-1);

		if (strstr(line, "Host:")) {
			pos = strcspn(line, " ");
			len = strlen(line) - pos;
			--len;
			dbg("Host len %u\n", len);
			
			Host = malloc(len + 1);
			if (!Host) {
				free(line);
				return(-1);
			}
			
			memset(Host, 0, len + 1);
			strncpy(Host, line + pos + 1, len);
			Host[len]='\0';
			dbg("Host %s\n", Host);
			strncpy(h, Host, MAX_LENGTH);
			free(Host);
		} else
		if (strstr(line, "Content-Type:")) {
			pos = strcspn(line, " ");
			len = strlen(line) - pos;
			--len;
			dbg("Content-Type len %u\n", len);
			
			Content_Type = malloc(len + 1);
			if (!Content_Type) {
				free(line);
				return(-1);
			}
			
			memset(Content_Type, 0, len + 1);
			strncpy(Content_Type, line + pos + 1, len);
			Content_Type[len]='\0';
			dbg("Content-Type %s\n", Content_Type);
			strncpy(c, Content_Type, MAX_LENGTH);
			free(Content_Type);
		}
		/*
		 * Other header fields of interest include
		 * a) User Agent
		 * b) Accept
		 * Consult RFC 2616 for definitions. These would require
		 * additional `else if strstr(...)` statements. 
		 */
		free(line);
	}
	return(0);
}

/* 
 * dump header on stderr		
 *
 * while (*data && len) {
 *	if (*data == '\n' || isprint(*data))
 *		fprintf(stderr, "%c", *data);
 *	*data++;
 *	len--;
 * }
 *
 */

static void process(char *s, int l, char *table) {
	
	unsigned int qlen;
	unsigned int rlen;
	
	Q_Decl(q,SOCK_RECV_BUF_LEN);
	char b[SOCK_RECV_BUF_LEN]; /* temporary buffer */
	unsigned int blen;

	char uri[MAX_LENGTH];
	char hst[MAX_LENGTH];
	char cnt[MAX_LENGTH];

	unsigned long requests;

	struct __hwdb_request *req;
	char *data;
	unsigned int len;

	int pos = 0;
	
	if (!s) /* unlikely */
		return;
	
	if (((unsigned int) l) < sizeof(struct __hwdb_request))
		return;
	
	memset(q, 0, SOCK_RECV_BUF_LEN);
	memset(r, 0, SOCK_RECV_BUF_LEN);
	memset(b, 0, SOCK_RECV_BUF_LEN);
	qlen = 0;
	rlen = 0;
	blen = 0;

	dbg("buffer size %d\n", l);
	
	requests = 0;

	while (pos < l) {
	
		if (((unsigned int) (l - pos)) < sizeof(struct __hwdb_request))
			break;

		memset(uri, 0, MAX_LENGTH);
		memset(hst, 0, MAX_LENGTH);
		memset(cnt, 0, MAX_LENGTH);
		
		req = (struct __hwdb_request *) (s + pos);
		data = (s + pos) + sizeof(struct __hwdb_request);
		len = req->len;
		if (req->key.protocol == 0) {
			dbg("dummy read\n");
			pos += (sizeof(struct __hwdb_request) + req->len);
			fprintf(stderr, "[%d out of %d]\n", pos, l);
			continue;
		}
		
		blen += sprintf(b + blen, "insert into %s values (", table);
		
		blen += sprintf(b + blen, "'%u', ", req->key.protocol);
		
		blen += sprintf(b + blen, "\""IP4_FMT"\", '%hu', ",
			IP4_ARG(&req->key.sa), ntohs(req->key.sp));
		
		blen += sprintf(b + blen, "\""IP4_FMT"\", '%hu', ",
			IP4_ARG(&req->key.da), ntohs(req->key.dp));
		
		parse_http_header(data, len, uri, hst, cnt);
		
		blen += sprintf(b + blen, "\"%s\", \"%s\", \"%s\")\n",
			(hst[0] != '\0' && strchr(hst, '"') == NULL) ? hst : "NULL",
			(uri[0] != '\0' && strchr(uri, '"') == NULL) ? uri : "NULL",
			(cnt[0] != '\0' && strchr(cnt, '"') == NULL) ? cnt : "NULL");
		
		pos += (sizeof(struct __hwdb_request) + req->len);
		fprintf(stderr, "[%d out of %d]\n", pos, l);
		requests++;
	}

	dbg("%ld requests processed\n", requests);
	if (requests == 0) /* unlikely */
		return;

	qlen += sprintf(q + qlen, "BULK:%ld\n", requests);
	qlen += sprintf(q + qlen, "%s", b);

	dbg("khttplogger[%u]>%s\n", qlen, q);
	if (! rpc_call(rpc, Q_Arg(q), qlen, r, sizeof(r), &rlen)) {
		fprintf(stderr, "rpc_call() failed\n");
	}
	dbg("hwdb[%u]>%s\n", rlen, r);
	return;
}

int main(int argc, char *argv[]) {
	
	char buffer[HWDB_CBSIZE];
	int l;
	
	int i, j;
	char *target = HWDB_SERVER_ADDR;
	unsigned short port = HWDB_SERVER_PORT;
	char *table = "Urls";
	char *device = "/dev/hwdb2";
	
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
	dbg("khttplogger reads\n");
	while (! must_exit) {
		memset(buffer, 0, sizeof(buffer));
		l = read(f, buffer, HWDB_CBSIZE);
		++count;
		if (l > 0)
			process(buffer, l, table);
	}

	close(f);
	rpc_disconnect(rpc);
	dbg("khttplogger exit [%llu]\n", count);
	exit(0);
}

