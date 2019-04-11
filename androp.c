/* Support for Android properties */

#ifdef ANDROID

#include <stdlib.h>
#include <string.h>

#include "cutils/properties.h"
#include "androp.h"

// every prop is stored as "name\0value\0"
// to have a static storage to return a pointer to
static char **stor;
// number of stored props
static int stor_size = 0;

void androp_init ()
{
}

void androp_fini ()
{
	if (stor_size) {
		for (int i = 0; i < stor_size; i++)
			free (stor [i]);
		free (stor);
		stor_size = 0;
	}
}

const char *androp_get (const char *key)
{
	char *val;
	int key_len = strlen (key);

	/* replace cached entry or create new */
	int i;
	for (i = 0; i < stor_size; i++)
		if (!strcmp (stor [i], key))
			goto found;

	stor_size++;
	stor = realloc (stor, stor_size * sizeof (char *));
	stor [i] = malloc (key_len + 1 + PROPERTY_VALUE_MAX + 1);
	memcpy (stor [i], key, key_len + 1);
found:
	val = stor [i] + key_len + 1;
	if (__system_property_get (key, val) <= 0) {
		*val = 0;
		return "";
	}

	return val;
}

#endif /* ANDROID */
