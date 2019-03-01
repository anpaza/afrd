#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "afrd.h"
#include "settings.h"

const char *cfg_get_str (const char *key, const char *defval)
{
	const char *ret;

	ret = settings_get (key);
	if (ret)
		return ret;

	ret = cfg_get (g_cfg, key);
	if (ret)
		return ret;

	return defval;
}

int cfg_get_int (const char *key, int defval)
{
	const char *ret = cfg_get_str (key, NULL);
	if (!ret)
		return defval;

	return atoi (ret);
}
