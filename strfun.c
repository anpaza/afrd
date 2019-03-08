// useful functions for handling strings

#include "afrd.h"

const char *spaces = " \t\r\n";

int strskip (const char *str, const char *starts)
{
	size_t sl1 = strlen (str);
	size_t sl2 = strlen (starts);
	if (sl1 < sl2)
		return 0;

	if (memcmp (str, starts, sl2) == 0)
		return sl2;

	return 0;
}

void strip_trailing_spaces (char *eol, const char *start)
{
	while (eol > start) {
		eol--;
		if (strchr (spaces, *eol) == NULL) {
			eol++;
			break;
		}
	}

	*eol = 0;
}

int parse_int (char **line)
{
	int v = 0, d;

	while (*line && (d = **line)) {
		d -= '0';
		if ((d < 0) || (d > 9))
			break;

		v = (v * 10) + d;
		(*line)++;
	}

	return v;
}

bool strlist_load (strlist_t *list, const char *key, const char *desc)
{
	list->size = 0;
	list->data = NULL;

	const char *str = cfg_get_str (key, NULL);
	if (!str)
		return false;

	if (desc)
		trace (1, "\tloading %s\n", desc);

	char *tmp = strdup (str);
	char *cur = tmp;
	while (*cur) {
		cur += strspn (cur, spaces);
		char *next = cur + strcspn (cur, spaces);
		if (*next)
			*next++ = 0;

		if (desc)
			trace (2, "\t+ %s\n", cur);

		list->size++;
		list->data = realloc (list->data, list->size * sizeof (char *));
		list->data [list->size - 1] = strdup (cur);

		cur = next;
	}

	free (tmp);
	return true;
}

void strlist_free (strlist_t *list)
{
	for (int i = 0; i < list->size; i++)
		free (list->data [i]);
	list->size = 0;
}

bool strlist_contains (strlist_t *list, const char *str)
{
	for (int i = 0; i < list->size; i++)
		if (!strcmp (list->data [i], str))
			return true;

	return false;
}

unsigned long find_ulong (const char *str, const char *prefix, bool *ok)
{
	if (!*ok)
		return false;

	const char *pfx = strstr (str, prefix);
	if (!pfx)
		goto fail;

	char *tmp;
	pfx += strlen (prefix);
	unsigned long val = strtoul (pfx, &tmp, 10);
	if (tmp > pfx)
		return val;

fail:	*ok = false;
	return 0;
}

unsigned long long find_ulonglong (const char *str, const char *prefix, bool *ok)
{
	if (!*ok)
		return false;

	const char *pfx = strstr (str, prefix);
	if (!pfx)
		goto fail;

	char *tmp;
	pfx += strlen (prefix);
	unsigned long val = strtoull (pfx, &tmp, 10);
	if (tmp > pfx)
		return val;

fail:	*ok = false;
	return 0;
}
