/*
 * HTTP Request Header Deep Packet Inspection
 *
 * urllogger inspects the payload of HTTP requests
 *
 */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <pcap.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <pthread.h>

#include "http_accumulator.h"
#include "srpc.h"
#include "config.h"

#define USAGE "./httplogger [-d device] [-f filter string]"

#define SIZE_ETHERNET 14
#ifndef ETHER_ADDR_LEN
#define ETHER_ADDR_LEN 6
#endif

#define HST_LEN 128 /* cf. http_accumulator.h */
#define URI_LEN 128
#define CNT_LEN 128

static int verbose = 2;

/* Ethernet header */
struct ethernet_header {
	u_char ether_dhost[ETHER_ADDR_LEN];
	u_char ether_shost[ETHER_ADDR_LEN];
	u_short ether_type;
};

/* IP header */
struct ip_header {
	u_char  ip_vhl;
	u_char  ip_tos;
	u_short ip_len;
	u_short ip_id;
	u_short ip_off;
#define IP_RF      0x8000
#define IP_DF      0x4000 /* don't fragment */
#define IP_MF      0x2000 /* more fragments */
#define IP_OFFMASK 0x1fff
	u_char  ip_ttl;
	u_char  ip_p;
	u_short ip_sum;
	struct  in_addr ip_src, ip_dst;
};

#define IP_HL(ip) (((ip)->ip_vhl) & 0x0f)
#define IP_V(ip)  (((ip)->ip_vhl) >> 4)

typedef u_int tcp_seq;

struct tcp_header {
	u_short th_sport;
	u_short th_dport;
	tcp_seq th_seq;
	tcp_seq th_ack;
	u_char  th_offx2;
#define TH_OFF(th) (((th)->th_offx2 & 0xf0) >> 4)
	u_char  th_flags;
#define TH_FIN  0x01
#define TH_SYN  0x02
#define TH_RST  0x04
#define TH_PUSH 0x08
#define TH_ACK  0x10
#define TH_URG  0x20
#define TH_ECE  0x40
#define TH_CWR  0x80
#define TH_FLAGS (TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)
	u_short th_win;
	u_short th_sum;
	u_short th_urp;
};

typedef struct frag* fragP;
typedef struct frag {
	fragP next;
	char *payload;
	int len;
	FlowKey key;
} frag_t;
#define NFRAGS 101
static fragP bin[NFRAGS];

void fragacc_init() {
	unsigned int i;
	for (i = 0; i < NFRAGS; i++)
		bin[i] = NULL;
}

void fragacc_free() {
	unsigned int i;
	fragP p, q;
	for (i = 0; i < NFRAGS; i++) {
		for (p = bin[i]; p != NULL; p = q) { 
			q = p->next;
			free(p->payload);
			free(p);
		}
	}
}

#define SHIFT 13
static unsigned int fragacc_hash(FlowKey *key) {
    unsigned int ans = 9;
    unsigned char *s = (unsigned char *) key;
    unsigned int i;
    for (i = 0; i < sizeof(FlowKey); i++)
		ans = (SHIFT * ans) + *s++;
    return ans % NFRAGS;
}

fragP fragacc_lookup(FlowKey key) {
	fragP p;
	unsigned int h = fragacc_hash(&key);
	for (p = bin[h]; p != NULL; p = p->next) {
		if (memcmp(&key, &(p->key), sizeof(FlowKey)) == 0) {
			return p;
		}
	}
	return NULL;
}

fragP fragacc_lookback(FlowKey key, fragP *prev) {
	fragP pr, cu;
	unsigned int h = fragacc_hash(&key);
	pr = NULL;
	cu = bin[h];
	while (cu != NULL) {
		if (memcmp(&key, &(cu->key), sizeof(FlowKey)) == 0) {
			*prev = pr;
			return cu;
		}
		pr = cu;
		cu = pr->next;
	}
	return NULL;
}

fragP frag_new(const char *payload, int len, FlowKey key) {
	if (verbose > 0) printf("new fragment\n");
	fragP p = malloc(sizeof(frag_t));
	if (!p) {
		fprintf(stderr, "Error: cannot allocate new fragment\n");
		exit(1);
	}
	p->payload = malloc(len+1);
	memset(p->payload, 0, len+1);
	strncpy(p->payload, payload, len);
	p->payload[len]='\0';
	p->len = len;
	p->key = key;
	unsigned int h = fragacc_hash(&key);
	p->next = bin[h];
	bin[h] = p;
	// Added.
	return p;
}

void frag_delete(fragP fr) {
	fragP p;
	fragP prev;
	p = fragacc_lookback(fr->key, &prev);
	if (!p) {
		fprintf(stderr, "Error: will not delete fragment\n");
		exit(1);
	}
	if (verbose > 0)
		printf("delete fragment (%d %d)\n", strlen(fr->payload), fr->len);
	
	unsigned long i = fragacc_hash(&(fr->key)); /* once again */
	if (prev == NULL) {
		bin[i] = p->next;
	} else
		prev->next = p->next;
	if (p->payload) free(p->payload);
	if (p) free(p);
}

void frag_append(fragP fr, const char *payload, int len) {
	if (verbose > 0) printf("append to fragment\n");
	char *p = malloc(fr->len + len + 1);
	if (!p) {
		fprintf(stderr, "Error: will not append to fragment\n");
		exit(1);
	}
	memset(p, 0, sizeof(p));
	strcpy(p, fr->payload);
	p = strncat(p, payload, len);
	p[fr->len + len + 1]='\0';
	if (fr->payload) free(fr->payload);
	fr->payload = NULL;
	fr->payload = p;
	fr->len = fr->len + len;
	return;
}

void frag_dump(fragP p) {
	char *src = strdup(inet_ntoa(*(struct in_addr *)(&(p->key.ip_src))));
	char *dst = strdup(inet_ntoa(*(struct in_addr *)(&(p->key.ip_dst))));
	fprintf(stderr, "%u: %s [%d] -> %s [%d] payload %d len %d\n", 
		p->key.proto, src, p->key.sport, dst, p->key.dport, 
		strlen(p->payload), p->len);
	free(src);
	free(dst);
}

void fragacc_dump() {
	int i;
	fragP p;
	fprintf(stderr, "pending fragments ---\n");
	for (i = 0; i < NFRAGS; i++) {
		for (p = bin[i]; p != NULL; p = p->next) { 
			frag_dump(p);
		}
	}
	fprintf(stderr, "---------------------\n");	
}

/* 
 *remove leading whitespace(s) from strings
 *
static char *trim(char *const s) {
	size_t len;
	char *cur;
	if(s && *s) {
		len = strlen(s);
		cur = s;
		while(*cur && isspace(*cur))
			++cur, --len;
		if(s != cur)
			memmove(s, cur, len + 1);
	}
	return s;
}
 *
 */

/*
 * remove host from uri
 *
static char *rem(char *const s) {
	size_t len;
	char *cur;
	if(s && *s) {
		len = strlen(s);
		cur = s;
		while(*cur && *cur != '/')
			++cur, --len;
		if(s != cur)
			memmove(s, cur, len + 1);
	}
	return s;
}
 *
 */

/* This filter should be a command line argument.
 * 
 * Since we look for HTTP requests, look at dest-
 * ination port only
 */
int ishttp(unsigned short dport) {
	return (dport == 80 || dport == 8080);
}

int ishttprequest(char *line) {
		return (
		strcmp( line, "GET"     ) == 0 || 
		strcmp( line, "OPTIONS" ) == 0 ||
		strcmp( line, "HEAD"    ) == 0 ||
		strcmp( line, "POST"    ) == 0 ||
		strcmp( line, "PUT"     ) == 0 ||
		strcmp( line, "DELETE"  ) == 0 ||
		strcmp( line, "TRACE"   ) == 0 ||
		strcmp( line, "CONNECT" ) == 0 );
}

void dump (char *payload, int len) {
	char *data = payload;
	while (*data && len) {
		if (*data == '\n' || isprint(*data)) 
			fprintf(stderr, "%c", *data);
		*data++;
		len--;
	}
	fprintf(stderr, " [%d]\n", len);
	return;
}

static char *readline(char *payload, int *offset) {
	char *prefix, *p;
	char *data = payload + *offset;
	char *eol = strstr(data, "\r\n"); /* crlf signifies end of line */
	if (!eol)
		return NULL;
	size_t linelength = eol - data;
	
	if (verbose > 1) printf("Line length %u\n", linelength);
	if (!linelength) {
		if (verbose > 1) printf("return NULL\n");
		return NULL;
	}

	*offset += linelength + 2; /* set next offset */
	
	prefix = (char *) malloc(linelength + 1); /* Add '\0' */
	if (!prefix)
		return NULL;

	memset(prefix, '\0', linelength + 1);
	p = prefix;
	while (*data && linelength > 0) {
		if (isprint(*data)) {
			*prefix++ = *data++;
			linelength--;
		} else
			break;
	}
	prefix[linelength]='\0';
	if (verbose > 1) printf("prefix %s\n", p);
	return p;
}

char *truncate(char *s) {
	char url[URI_LEN];
	size_t len;
	if (!s)
		return NULL;
	memset(url, '\0', URI_LEN);
	len = strlen(s);
	if (len < URI_LEN) strncpy(url, s, URI_LEN-1);
	else {
		strncpy(url, s, URI_LEN - 6); /* to append [...] */
		strncpy(url + URI_LEN - 6, "[...]", 5);
	}
	free(s);
	return strdup(url);
}

static void parse_http_header(fragP f) {

	char *URI = NULL;
	char *Host = NULL;
	char *Content_Type = NULL;
	
	size_t pos, len;

	int offset = 0;

	char *request_line = readline(f->payload, &offset);
	if (!request_line) {
		if (verbose > 1) printf("NULL returned\n");
		return;
	}
	
	pos = 0;
	pos = strcspn(request_line + pos + 0, " "); /* 1st space */
	len = strcspn(request_line + pos + 1, " "); /* 2nd space */
	if (!len) {
		free(request_line);
		return;
	}
	
	URI = malloc(len + 1);
	if (!URI) {
		free(request_line);
		return; /* out of memory */
	}

	memset(URI, 0, len + 1);
	strncpy(URI, request_line + pos + 1, len);
	URI[len]='\0';
	if (verbose > 1) printf("URI %s\n", URI);
	
	free(request_line);

	while (offset < f->len) {
		
		char *line = readline(f->payload, &offset);
		if (!line) {
			if (verbose > 1) printf("goto error\n");
			break;
			// goto error;
		}
		if (strstr(line, "Host:")) {
			pos = strcspn(line, " ");
			len = strlen(line) - pos;
			--len;
			if (verbose > 1) printf("Host len %u\n", len);
			
			Host = malloc(len + 1);
			if (!Host) {
				free(line);
				goto error;
			}
			
			memset(Host, 0, len + 1);
			strncpy(Host, line + pos + 1, len);
			Host[len]='\0';
			if (verbose > 1) printf("Host %s\n", Host);
		} else
		if (strstr(line, "Content-Type:")) {
			pos = strcspn(line, " ");
			len = strlen(line) - pos;
			--len;
			if (verbose > 1) printf("Content-Type len %u\n", len);
			
			Content_Type = malloc(len + 1);
			if (!Content_Type) {
				free(line);
				goto error;
			}
			
			memset(Content_Type, 0, len + 1);
			strncpy(Content_Type, line + pos + 1, len);
			Content_Type[len]='\0';
			if (verbose > 1) printf("Content-Type %s\n", Content_Type);
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
	
	if (!Host) { /* "Host:" MUST be defined (cf. RFC 2616) */
		fprintf(stderr, "Error: no host found in request\n");
		goto error;
	}

	if (!Content_Type) {
		if (verbose > 1) printf("Set content-type to NULL\n");
		Content_Type = malloc(5);
		memset(Content_Type, 0, 5);
		sprintf(Content_Type, "%s", "NULL");
	}

	URI = truncate(URI);
	
	url_update(&(f->key), Host, URI, Content_Type);
error:
	if (URI)
		free(URI);
	if (Host) 
		free(Host);
	if (Content_Type)
		free(Content_Type);
	
	return;
}

void parse(const char *payload, int len, FlowKey key) {
	char *method;
	size_t length;	
	char *eof;
	fragP fr = NULL;
	fr = fragacc_lookup(key);
	eof = strstr(payload, "\r\n\r\n");
	if (fr) { /* append to end of fragment */
		if (verbose > 0) printf("fragment exists\n");
		frag_append(fr, payload, len);
	} else {
		/* the first string is the method */
		length = strcspn(payload, " ");
		if (verbose > 0) fprintf(stderr, "length=%d\n", length);
		if (length > 0) {
			method = malloc(length +1);
			strncpy(method, payload, length);
			method[length]='\0';
			if (ishttprequest(method)) {
				if (verbose > 0) 
					fprintf(stderr, "new header; method is %s\n", method);
				fr = frag_new(payload, len, key);
			} else {
				if (verbose > 0)
					fprintf(stderr, "Ignore packet\n");
				free(method);
				return;
			}
			free(method);
		}
	}
	/* Is the header fragmented */
	if (eof) {
		if (verbose > 0) printf("eof found\n");
		parse_http_header(fr);
		frag_delete(fr);
	} else {
		if (verbose > 0) printf("awaiting next packet...\n");
	}
	if (verbose > 0) fragacc_dump();
	return;
}

void my_callback (u_char *args, const struct pcap_pkthdr* pkthdr,
	const u_char* packet) {
	const struct ethernet_header *ethernet;
	const struct ip_header *ip;
	const struct tcp_header *tcp;
	const char *payload;
	int sizeof_ip, sizeof_tcp, sizeof_payload; 
	ethernet = (struct ethernet_header *) packet;
	/* In a simpler world... */
	ip = (struct ip_header *) (packet + SIZE_ETHERNET);
	/* ...but, the goal is DPI. */
	sizeof_ip = IP_HL(ip) * 4;
	if (sizeof_ip < 20) {
		if (verbose > 0)
			fprintf( stderr, 
"Error: IP header is %u (< 20) bytes long.\n", sizeof_ip );
		return;
	}
	switch(ip->ip_p) {
		case IPPROTO_TCP: /* inspect TCP packets */
			break;
		case IPPROTO_UDP:
			return;
		default:
			return;
	}
	tcp = (struct tcp_header *) (packet + SIZE_ETHERNET + sizeof_ip);
	sizeof_tcp = TH_OFF(tcp) * 4;
	if (sizeof_tcp < 20) {
		if (verbose > 0)
			fprintf( stderr, 
"Error: TCP header is %u (< 20) bytes long.\n", sizeof_tcp );
		return;
	}
	if( !ishttp(ntohs(tcp->th_dport)) )
		return;
	if (tcp->th_flags & TH_SYN) {
		if (verbose > 0) fprintf(stderr, "SYN\n");
	}
	payload = (char *) (packet + SIZE_ETHERNET + sizeof_ip + sizeof_tcp);
	/* Compute (segment) size */
	sizeof_payload = ntohs(ip->ip_len) - (sizeof_ip + sizeof_tcp);
	if (sizeof_payload == 0)
		return;
	if (verbose > 0) 
		fprintf(stderr, "payload size is %d bytes\n", sizeof_payload);
	FlowKey key;
	memset(&key, 0, sizeof(FlowKey)); /* clear the bytes */
    key.ip_src = ip->ip_src;
    key.ip_dst = ip->ip_dst;
    key.sport = ntohs(tcp->th_sport);
    key.dport = ntohs(tcp->th_dport);
    key.proto = ip->ip_p;
	if (verbose > 0) {
		char *src = strdup(inet_ntoa(*(struct in_addr *)(&(ip->ip_src))));
		char *dst = strdup(inet_ntoa(*(struct in_addr *)(&(ip->ip_dst))));
		fprintf(stderr, "%u: %s [%d] -> %s [%d]\n", 
			ip->ip_p, src, ntohs(tcp->th_sport), dst, ntohs(tcp->th_dport));
		free(src);
		free(dst);
	}
	/* process http header segment, if any */
	parse (payload, sizeof_payload, key);
	if (args) {;}
	if (pkthdr) {;}	
	return;
}

static pcap_t* task;

#define QUERY_SIZE 50000
#define MAX_INSERTS (QUERY_SIZE/240) /* URL is 128 chars long. */
static RpcConnection rpc;
static struct timespec time_delay = {5, 0}; /* delay 1 second */
static char resp[32768];
static unsigned long long dropped = 0;
/*
 * thread that communicates new flow tuples to HWDB
 */
static void *handler(void *args) {
	int size;
	unsigned long nelems;
	UrlElem **buckets;
	UrlElem *returns, *p, *q;
	int i;
	unsigned int sofar;
	unsigned int len;
	struct pcap_stat stats;
	unsigned long long curdrop;
	unsigned long ninserts, dp;
	char drops[16];
	Q_Decl(buf,QUERY_SIZE);

	for (;;) {
	nanosleep(&time_delay, NULL); /* sleep for X second */
	buckets = url_swap(&size, &nelems);
	pcap_stats(task, &stats); /* determine if any dropped pkts */
	curdrop = (unsigned long long)(stats.ps_drop);
	if (curdrop > dropped) {
		dp = (unsigned long) (curdrop - dropped);
		dropped = curdrop;
		fprintf(stderr, "Warning: HTTP DPI logger drops %ld\n", dp);
	} else
		dp = 0;
	if (nelems <= 0 && dp == 0) /* nothing to do */
		continue;
	ninserts = nelems + (dp ? 1l : 0);
	sofar = 0;
	sofar += sprintf(buf+sofar, "BULK:%ld\n", ninserts);
	returns = NULL;
	if (nelems > 0) {
		for (i = 0; i < size; i++) {
			p = buckets[i];
			while (p != NULL) {
				FlowKey *f = &(p->id);
				q = p->next;
				sofar += sprintf(buf+sofar,
				"insert into urls values ('%u', ", f->proto);
				sofar += sprintf(buf+sofar, "\"%s\", '%hu', ",
				inet_ntoa(f->ip_src), f->sport);
				sofar += sprintf(buf+sofar, "\"%s\", '%hu', ",
				inet_ntoa(f->ip_dst), f->dport);
				sofar += sprintf(buf+sofar, "\"%s\",",
				p->hst);
				sofar += sprintf(buf+sofar, "\"%s\",",
				p->uri);
				sofar += sprintf(buf+sofar, "\"%s\")\n",
				p->cnt);
				p->next = returns;
				returns = p;
				p = q;
			}
		}
		url_freeChain(returns);
	}
	if (dp > 0) {
	sprintf(drops, "%ld", dp);
	sofar += sprintf(buf+sofar, "insert into Urls values ('0', ");
	sofar += sprintf(buf+sofar, "\"0.0.0.0\", '0', ");
	sofar += sprintf(buf+sofar, "\"0.0.0.0\", '0', ");
	sofar += sprintf(buf+sofar, "\"%s\", \"unknown\", \"unknown\")\n", drops);
	}
	if (verbose > 0) 
		printf("%s (%d)", buf, strlen(buf));
	if (! rpc_call(rpc, Q_Arg(buf), sofar, resp, sizeof(resp), &len)) {
		fprintf(stderr, "rpc_call() failed\n");
	}

    }
    return (args) ? NULL : args; /* unused warning subterfuge */
}

int main (int argc, char *argv[]) {
	char *dev;
	char *pfp;
	char err[PCAP_ERRBUF_SIZE];
	struct bpf_program fp;
	bpf_u_int32 msk;
	bpf_u_int32 net;
	u_char* args = NULL;
	int i, j;
	pthread_t thr;
	char *target;
	unsigned short port;
    dev = "wlan0";
    pfp = "ip";
    target = HWDB_SERVER_ADDR;
    port = HWDB_SERVER_PORT;
	for (i = 1; i < argc; ) {
		if ((j = i + 1) == argc) {
			fprintf(stderr, "usage: %s\n", USAGE);
			exit(1);
		}
		if (strcmp(argv[i], "-d") == 0)
			dev = argv[j];
		else if (strcmp(argv[i], "-f") == 0)
			pfp = argv[j];
		else {
			fprintf(stderr, "Unknown flag: %s %s\n", argv[i], argv[j]);
		}
		i = j + 1;
	}

	if (! rpc_init(0)) {
		fprintf(stderr, "Initialization failure for rpc system\n");
		exit(-1);
	}
	rpc = rpc_connect(target, port, "HWDB", 1l);
	if (rpc == NULL) {
		fprintf(stderr, "Error connecting to HWDB at %s:%05u\n", target, port);
		exit(-1);
	}
	url_init();
	
	if (pcap_lookupnet(dev, &net, &msk, err) == -1) {
		printf("Lookup error (%s): %s\n", dev, err);
		net = 0;
		msk = 0;
	}
	task = pcap_open_live(dev, BUFSIZ, 1, -1, err);
	if (task == NULL) {
		printf( "Device error (%s): %s\n", dev, err);
		return -1;
	}
	
	if(pfp != NULL) {
		if(pcap_compile(task, &fp, pfp, 0, net) == -1) {
			fprintf(stderr, "Error compiling filter\n");
			exit(1);
		}
		/* set the compiled program as the filter */
		if(pcap_setfilter(task, &fp) == -1) {
			fprintf(stderr, "Error setting filter\n");
			exit(1);
		}
	}
	/* reset http flows */
	fragacc_init();
	/* create periodic thread */
	if (pthread_create(&thr, NULL, handler, NULL)) {
		fprintf(stderr, "Failure to start database thread\n");
		exit(1);
	}
	/* ... and loop */
	pcap_loop(task, -1, my_callback, args);
	pcap_close(task);
	return 0;
}

