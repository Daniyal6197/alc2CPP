/*
 * program to translate BerkeleyDB file for Flows to new format on stdout
 */
#include "flowrec.h"
#include "timestamp.h"
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <db.h>
#include <sys/time.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "portmap.h"
#include "hostmap.h"
#include "protomap.h"

#define USAGE "./flowtransl8 -d databasename"

int main(int argc, char *argv[]) {
    int i, j;
    char *database;
    DB *dbp;
    DBT key, data;
    DBC *dbcp;
    int ret;
    FlowData fr;

    database = NULL;
    for (i = 1; i < argc; ) {
        if ((j = i + 1) == argc) {
            fprintf(stderr, "usage: %s\n", USAGE);
            exit(1);
        }
        if (strcmp(argv[i], "-d") == 0)
            database = argv[j];
	else {
            fprintf(stderr, "Unknown flag: %s %s\n", argv[i], argv[j]);
        }
        i = j + 1;
    }
    if (database == NULL) {
        fprintf(stderr, "usage: %s\n", USAGE);
	exit(1);
    }
    if ((ret = db_create(&dbp, NULL, 0)) != 0) {
        fprintf(stderr, "db_create: %s\n", db_strerror(ret));
        exit(1);
    }
    if ((ret = dbp->open(dbp, NULL, database, NULL,
			 DB_RECNO, 0, 0664)) != 0) {
        dbp->err(dbp, ret, "%s", database);
        goto err;
    }
    if ((ret = dbp->cursor(dbp, NULL, &dbcp, 0)) != 0) {
        dbp->err(dbp, ret, "DB->cursor");
	goto err;
    }
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    data.flags = DB_DBT_USERMEM;
    data.data = &fr;
    data.ulen = sizeof(fr);
    while ((ret = dbcp->get(dbcp, &key, &data, DB_NEXT)) == 0) {
	char *s = timestamp_to_string(fr.tstamp);
	char *src = strdup(inet_ntoa(*(struct in_addr *)(&(fr.ip_src))));
	char *dst = strdup(inet_ntoa(*(struct in_addr *)(&(fr.ip_dst))));
        printf("%s<|>%u<|>%s<|>%hu<|>%s<|>%hu<|>%lu<|>%lu<|>\n", s, fr.proto,
	       src, fr.sport, dst, fr.dport, fr.packets, fr.bytes);
	free(s);
	free(src);
	free(dst);
    }
    if (ret != DB_NOTFOUND)
	dbp->err(dbp, ret, "DBcursor->get");
    if ((ret = dbcp->close(dbcp)) != 0)
	dbp->err(dbp, ret, "DBcursor->close");
err:
    ret = dbp->close(dbp, 0);
    exit(ret);
}
