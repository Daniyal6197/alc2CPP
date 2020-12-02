#ifndef _CONFIG_H_
#define _CONFIG_H_


/* Server */
#define HWDB_SERVER_ADDR "localhost"
#define HWDB_SERVER_PORT 987
#define HWDB_SNAPSHOT_PORT 988
#define HWDB_PERSISTSERVER_PORT 990

#define SOCK_RECV_BUF_LEN 65535
#define RTAB_MSG_MAX_LENGTH 100

/* Index */
#define TT_BUCKETS 100

/* Saved-queries hashtable */
#define HT_QUERYTAB_BUCKETS 100

/* Multi-threading */
#define HWDB_PUBLISH_IN_BACKGROUND
#define NUM_THREADS 1			/* cannot change this! */

#endif	/* _CONFIG_H_ */
