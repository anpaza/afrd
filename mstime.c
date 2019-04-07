/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Millisecond timers
 */

#include "mstime.h"
#include <stdlib.h>
#include <sys/time.h>

mstime_t g_mstime;

mstime_t mstime_get ()
{
	struct timeval tv;
	gettimeofday (&tv, NULL);
	return tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}
