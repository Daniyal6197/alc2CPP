#ifndef _UTIL_H_
#define _UTIL_H_

#include "mem.h"
#include "config.h"
#include "logdefs.h"

#include <stdio.h>

/* -------- [MESSAGE] -------- */
#ifdef NMSG
#define MSG (void)
#else  /* NMSG */
#define MSG printf("DBSERVER> "); printf
#endif /* NMSG */

#endif /* _UTIL_H_ */
