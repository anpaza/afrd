/*
 * Millisecond timers
 * Copyright (c) 2017 Andrew Zabolotny
 *
 * This code can be freely redistributed under the terms of
 * GNU Less General Public License version 3 or later.
 */

#include "mstime.h"
#include <sys/time.h>

mstime_t g_mstime;

mstime_t mstime_get ()
{
	struct timeval tv;
	gettimeofday (&tv, NULL);
	return tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}
