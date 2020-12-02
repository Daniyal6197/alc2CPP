/*
 * interface and data structures associated with the extended flow accumulator
 */
#ifndef _NFLOWACCUMULATOR_H_
#define _NFLOWACCUMULATOR_H_

#include "timestamp.h"
#include <netinet/ip.h> /* defines in_addr */

#define NPKTS 10
#define TIMER 15000000000LL 

typedef struct flow_rec {
   struct in_addr ip_src;
   struct in_addr ip_dst;
   unsigned short sport;
   unsigned short dport;
   unsigned char proto;
} FlowRec;

typedef struct flow_packet {
   /* timestamp & the size of the first 10 packets of a flow */
   tstamp_t tstamp;
   unsigned long bytes;
   unsigned char flags;
} FlowPkt;

typedef struct acc_elem {
   struct acc_elem *next;
   FlowRec id;
   unsigned long packets;
   unsigned long bytes;
   FlowPkt pkts[NPKTS+1]; /* store first N=10 packets, then accumulate */
   unsigned char or_flags;
} AccElem;

/*
 * initialize the data structures
 */
void acc_init();

/*
 * update packet and byte counts associated with particular flow
 * creating flow record if first time flow has been seen
 */
void acc_update(struct in_addr src, struct in_addr dst,
		unsigned short sport, unsigned short dport,
		unsigned long proto, unsigned long bytes,
		unsigned char flags, tstamp_t timestamp);

/*
 * swap accumulator tables, returning previous table for processing
 */
AccElem **acc_swap(int *size, unsigned long *nelems, unsigned long *felems);

/*
 * add chain of elements to free list
 */
void acc_freeChain(AccElem *elem);

/*
 * dump accumulator
 */
void acc_dump();

/*
 * no idea what this function does
 */
int acc_element_finished(AccElem *p);

#endif /* _NFLOWACCUMULATOR_H_ */
