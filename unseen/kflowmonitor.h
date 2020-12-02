#ifndef _KFLOWMONITOR_H_
#define _KFLOWMONITOR_H_

#include "flowrec.h"
#include <netinet/ip.h>	/* defines in_addr */
#include "timestamp.h"

typedef struct kflows {
   unsigned long nflows;
   KFlow **data;
} KFlows;

KFlows *mon_convert(Rtab *results);

void mon_free(KFlows *p);

#define FIN 0x01 /* TCP flags */
#define SYN 0x02
#define RST 0x04
#define PSH 0x08
#define ACK 0x10
#define URG 0x20
#define ECE 0x40
#define CWR 0x80

#define FLG_FMT "%s%s%s%s%s%s%s%s"

#define FLG_ARG(flags) \
	(flags & ACK) ? ":ACK" : "", \
	(flags & SYN) ? ":SYN" : "", \
	(flags & RST) ? ":RST" : "", \
	(flags & PSH) ? ":PSH" : "", \
	(flags & FIN) ? ":FIN" : "", \
	(flags & URG) ? ":URG" : "", \
	(flags & ECE) ? ":ECE" : "", \
	(flags & CWR) ? ":CWR" : ""

#endif /* _KFLOWMONITOR_H_ */
