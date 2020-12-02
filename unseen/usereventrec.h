#ifndef _USEREVENTREC_H_
#define _USEREVENTREC_H_

#include "timestamp.h"

typedef struct user_event {
   char application[16];
   char logtype[16];
   char logdata[128];
   tstamp_t tstamp;
} UserEvent;

#endif /* _USEREVENTREC_H_ */
