#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "afrd.h"

const char *cfg_get_str (const char *key, const char *defval)
{
	const char *ret = cfg_get (g_cfg, key);
	if (!ret)
		return defval;
	return ret;
}

int cfg_get_int (const char *key, int defval)
{
	const char *ret = cfg_get (g_cfg, key);
	if (!ret)
		return defval;

	return atoi (ret);
}
