/*
 * data structure associated with dhcp record
 */
#ifndef _DHCPREC_H_
#define _DHCPREC_H_

#include "timestamp.h"
#include "rtab.h"
#include <stdint.h>	/* to access definition of uint32_t */
#include <netinet/in.h>	/* to access definition of in_addr_t */

typedef struct dhcp_data {
   unsigned int action;	/* 0:add 1:del 2:old */
   uint64_t mac_addr;
   in_addr_t ip_addr;
   char hostname[80];
   tstamp_t tstamp;
} DhcpData;

/*
 * convert Rtab results into DhcpData
 */
DhcpData *dhcp_convert(Rtab *results);

/*
 * free heap storage associated with DhcpData
 */
void dhcp_free(DhcpData *p);

unsigned int action2index(char *action);
char *index2action(unsigned int index);

#endif /* _DHCPREC_H_ */
