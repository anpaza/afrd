/* Support for Android settings via external tool */

#ifdef ANDROID

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/fcntl.h>

#include "afrd.h"
#include "settings.h"

#define KEY_MAX		64
#define VAL_MAX		256

// every setting is cached as "key\0value\0"
static char **stor;
// number of cached props
static int stor_size = 0;
// /system/bin/settings or NULL
static const char *g_settings_cmd;

void settings_init ()
{
	g_settings_cmd = cfg_get_str ("settings.cmd", NULL);
	if (!*g_settings_cmd)
		g_settings_cmd = NULL;
}

void settings_fini ()
{
	if (stor_size) {
		for (int i = 0; i < stor_size; i++)
			free (stor [i]);
		free (stor);
		stor_size = 0;
	}
}

static bool run_cmd (const char *cmd [], char *res, int res_max)
{
	bool rc = false;
	int wstatus;
	int out [2];
	if (pipe (out) != 0)
		return false;

	pid_t pid = fork ();
	if (pid == 0) {
		/* child process */
		dup2 (out [1], STDOUT_FILENO);
		exit (execv (cmd [0], (char *const *)cmd));
	}

	/* parent process */
	if (waitpid (pid, &wstatus, 0) < 0)
		goto leave;

	if (!WIFEXITED (wstatus) || (WEXITSTATUS (wstatus) != 0)) {
		trace (1, "WARNING: Unusable %s, Android settings will not work\n", cmd [0]);
		g_settings_cmd = NULL;
		return false;
	}

	fcntl (out [0], F_SETFL, O_NONBLOCK);
	int n = read (out [0], res, res_max - 1);
	if (n <= 0)
		goto leave;

	strip_trailing_spaces (res + n, res);
	if ((res [0] == 0) || !strcmp (res, "null"))
		goto leave;

	rc = true;

leave:
	close (out [0]);
	close (out [1]);

	return rc;
}

const char *settings_get (const char *key)
{
	if (!g_settings_cmd)
		return NULL;

	const char *cmdline [5];
	char *val;
	int key_len = strlen (key);
	FILE *sf;

	/* replace cached entry or create new */
	int i;
	for (i = 0; i < stor_size; i++)
		if (!strcmp (stor [i], key))
			goto found;

	stor_size++;
	stor = realloc (stor, stor_size * sizeof (char *));
	stor [i] = malloc (key_len + 1 + VAL_MAX + 1);
	memcpy (stor [i], key, key_len + 1);
found:
	val = stor [i] + key_len + 1;
	/* run command */
	cmdline [0] = g_settings_cmd;
	cmdline [1] = "get";
	cmdline [2] = "system";
	cmdline [3] = key;
	cmdline [4] = NULL;
	if (!run_cmd (cmdline, val, VAL_MAX + 1))
		return NULL;

	return val;
}

const char *settings_afrd_get (const char *key)
{
	int i;
	char afrd_key [KEY_MAX + 1];
	/* look for a property named "afrd.<key>" */
	snprintf (afrd_key, sizeof (afrd_key), "afrd_%s", key);
	/* replace dots with underscores */
	for (i = 5; afrd_key [i]; i++)
		if (afrd_key [i] == '.')
			afrd_key [i] = '_';

	return settings_get (afrd_key);
}

int settings_get_int (const char *key, int defval)
{
	const char *ret = settings_get (key);
	if (!ret)
		return defval;

	return atoi (ret);
}

#endif /* ANDROID */
