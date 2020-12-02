#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "srpc.h"
#include "config.h"
#include "rtab.h"

/*
 * store DHCP events in hwdb
 */

#define DATABASE_NAME "data/dhcp.db"
#define TEXTFILE_NAME "data/dhcp.txt"

int main(int argc, char *argv[]) {
	char *host;
	unsigned short port;
	RpcConnection rpc;
	Q_Decl(query,SOCK_RECV_BUF_LEN);
	char resp[SOCK_RECV_BUF_LEN];
	host = HWDB_SERVER_ADDR;
	port = HWDB_SERVER_PORT;
	unsigned int bytes;
	unsigned int len;
	char stsmsg[RTAB_MSG_MAX_LENGTH];

	/* check for correct number and type of arguments */
	if (argc < 4) {
                fprintf(stderr, "dnsmasq does not comply with hwdb schema.\n");
		return 0;
	}

	/* Connect to HWDB server */
	if (!rpc_init(0)) {
                fprintf(stderr, "Failure to initialize rpc system\n");
		return 0;
	}
	if (!(rpc = rpc_connect(host, port, "HWDB", 1l))) {
                fprintf(stderr, "Failure to connect to HWDB at %s:%05u\n",
			host, port);
		return 0;
	}

	/* process event (lease info) */
	bytes = 0;
	bytes += sprintf(query + bytes, "SQL:insert into Leases values (" );
	/* mac address */
	bytes += sprintf(query + bytes, "\"%s\", ", argv[2]);
	/* ip address */
	bytes += sprintf(query + bytes, "\"%s\", ", argv[3]);
	/* hostname (optional) */
	if (argc == 5) {
		bytes += sprintf(query + bytes, "\"%s\", ", argv[4]);
	} else {
		bytes += sprintf(query + bytes, "\"NULL\", ");
	}
	/* action */
	bytes += sprintf(query + bytes, "\"%s\") on duplicate key update\n", argv[1]);

	if (! rpc_call(rpc, Q_Arg(query), bytes, resp, sizeof(resp), &len)) {
		fprintf(stderr, "rpc_call() failed\n");
		rpc_disconnect(rpc);
		return 0;
	}
	resp[len] = '\0';
	if (rtab_status(resp, stsmsg))
		fprintf(stderr, "RPC error: %s\n", stsmsg);
	rpc_disconnect(rpc);

	return 0;
}

