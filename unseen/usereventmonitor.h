/*
 * interface and data structures associated with userevent monitor
 */
#ifndef _USEREVENTMONITOR_H_
#define _USEREVENTMONITOR_H_

#include "usereventrec.h"
#include "timestamp.h"

typedef struct user_event_results {
   unsigned long nevents;
   UserEvent **data;
} UserEventResults;

UserEventResults *mon_convert(Rtab *results);
void             mon_free(UserEventResults *p);

#endif /* _USEREVENTMONITOR_H_ */
