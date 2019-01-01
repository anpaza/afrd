#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "afrd.h"

char *sysfs_read (const char *device_attr)
{
	int h, n;
	char tmp [4096];

	h = open (device_attr, O_RDONLY);
	if (h < 0)
		goto error;

	n = read (h, tmp, sizeof (tmp) - 1);
	tmp [n] = 0;

	close (h);
	return strdup (tmp);

error:
	trace (1, "failed to read sysfs attr from %s\n", device_attr);

	if (h >= 0)
		close (h);
	return NULL;
}

char *sysfs_get_str (const char *device, const char *attr)
{
	char *ret;
	if (attr) {
		char tmp [200];
		snprintf (tmp, sizeof (tmp), "%s/%s", device, attr);
		ret = sysfs_read (tmp);
	} else
		ret = sysfs_read (device);

	// remove trailing spaces
	char *eol = strchr (ret, 0);
	while ((eol > ret) && strchr ("\r\n\t ", eol [-1]))
		eol--;
	*eol = 0;
	return ret;
}

int sysfs_get_int (const char *device, const char *attr)
{
	int val;
	char *vals = sysfs_get_str (device, attr);
	if (!vals)
		return -1;

        /* may be something like HDMI=1 */
        char *eq = strchr (vals, '=');
        if (eq != NULL)
            vals = eq + 1;

	val = strtol (vals, NULL, 0);
	free (vals);

	return val;
}

int sysfs_write (const char *device_attr, const char *value)
{
	int h, n;

	h = open (device_attr, O_TRUNC | O_WRONLY);
	if (h < 0)
		goto error;

	n = strlen (value);
	if (write (h, value, n) != n)
		goto error;

	close (h);
	return 0;

error:
	trace (1, "failed to write attr [%s] into %s\n", value, device_attr);

	if (h >= 0)
		close (h);
	return -1;
}

int sysfs_set_str (const char *device, const char *attr, const char *value)
{
	if (attr) {
		char tmp [200];
		snprintf (tmp, sizeof (tmp), "%s/%s", device, attr);
		return sysfs_write (tmp, value);
	} else
		return sysfs_write (device, value);
}

int sysfs_set_int (const char *device, const char *attr, int value)
{
	char tmp [11];
	snprintf (tmp, sizeof (tmp), "%d", value);
	return sysfs_set_str (device, attr, tmp);
}

int sysfs_exists (const char *device_attr)
{
	return access (device_attr, F_OK);
}
