/*
 * Automatic Framerate Daemon
 * for AMLogic S905/S912-based boxes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "afrd.h"

const char *g_version = "0.1.0";
const char *g_config = "/etc/afrd.ini";
const char *g_pidfile =
#ifdef ANDROID
	// android has no standard directory for pid files...
	// so use /dev/pid/ on tmpfs
	"/dev/run/afrd.pid";
#else
	"/var/run/afrd.pid";
#endif
const char *g_program;
int g_verbose = 0;
int g_daemon = 0;
int g_kill_daemon = 0;
volatile int g_shutdown = 0;

// the global config
struct cfg_struct *g_cfg = NULL;

static void show_version ()
{
	printf ("afr daemon version %s\n", g_version);
}

static void show_help (char *const *argv)
{
	show_version ();
	printf ("usage: %s [options] [config-file]\n", argv [0]);
	printf ("	-D	daemonize the program\n");
	printf ("	-p FILE	write PID to file when running as daemon\n");
	printf ("	-k	kill the running daemon\n");
	printf ("	-h	display this help\n");
	printf ("	-v	verbose info about what's cooking\n");
	printf ("	-V	display program version\n");
}

void trace (int level, const char *format, ...)
{
	va_list argp;
	struct tm tm;
	struct timeval tv;

	if (level > g_verbose)
		return;

	if (gettimeofday (&tv, NULL) < 0)
		return;

	localtime_r (&tv.tv_sec, &tm);

	printf ("%02d:%02d:%02d.%03d ", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(tv.tv_usec / 1000));
	va_start (argp, format);
	vprintf (format, argp);
	va_end (argp);
}

static void signal_handler (int sig)
{
	g_shutdown = 1;
}

static int load_config (const char *config)
{
	int ret;

	trace (1, "loading config file '%s'\n", config);

	g_cfg = cfg_init ();

	if ((ret = cfg_load (g_cfg, config)) < 0) {
		cfg_free (g_cfg);
		g_cfg = NULL;
		fprintf (stderr, "%s: failed to load config file '%s'\n",
			g_program, config);
		return ret;
	}

	trace (1, "\tsuccess\n");

	return 0;
}

static void daemonize ()
{
	pid_t pid;

	g_verbose = 0;

	pid = fork ();
	if (pid < 0) {
		fprintf (stderr, "%s: can't demonize, aborting\n", g_program);
		exit (-1);
	}

	if (pid != 0) {
#ifdef ANDROID
		if (strncmp (g_pidfile, "/dev/run/", 9) == 0)
			mkdir ("/dev/run", 0755);
#endif
		int h = open (g_pidfile, O_CREAT | O_WRONLY, 0644);
		if (h >= 0) {
			char tmp [10];
			write (h, tmp, snprintf (tmp, sizeof (tmp), "%d", pid));
			close (h);
		} else
			fprintf (stderr, "%s: failed to write pid file '%s'\n",
				g_program, g_pidfile);

		/* successfuly daemonized */
		exit (2);
	}

	/* continuing in child, close console */
	close (0);
	close (1);
	close (2);
}

static int kill_daemon ()
{
	char tmp [11];
	int h, n, ok = -1;
	pid_t pid = -1;

	h = open (g_pidfile, O_RDONLY);
	if (h < 0) {
		fprintf (stderr, "%s: failed to read pid from file '%s'\n",
			g_program, g_pidfile);
		return -1;
	}

	n = read (h, tmp, sizeof (tmp) - 1);
	if (n > 0) {
		tmp [n] = 0;
		pid = strtoul (tmp, NULL, 0);
		if (pid > 1)
			ok = kill (pid, SIGINT);
	}

	close (h);

	if (ok != 0)
		fprintf (stderr, "%s: failed to kill daemon pid %d\n",
			g_program, pid);

	return ok;
}

int main (int argc, char *const *argv)
{
	int ret;

	g_program = argv [0];

	while ((ret = getopt (argc, argv, "Dp:khvV")) >= 0)
		switch (ret) {
			case 'D':
				g_daemon = 1;
				break;

			case 'p':
				g_pidfile = optarg;
				break;

			case 'k':
				g_kill_daemon = 1;
				break;

			case 'v':
				g_verbose++;
				break;

			case 'h':
				show_help (argv);
				return -1;

			case 'V':
				show_version ();
				return -1;
		}

	if (g_kill_daemon)
		return kill_daemon ();

	if (g_daemon)
		daemonize ();

	// load the config files mentioned on command line, until one loads
	while (optind < argc)
		if ((ret = load_config (argv [optind++])) == 0)
			break;

        // as a last resort, try to load default config
	if (!g_cfg && (ret = load_config (g_config)) != 0) {
		fprintf (stderr, "%s: failed to load any config files\n",
			g_program);
		goto leave;
	}

	if ((ret = afrd_init ()) < 0)
		goto leave;

	signal (SIGINT,  signal_handler);
	signal (SIGQUIT, signal_handler);
	signal (SIGKILL, signal_handler);
	signal (SIGTERM, signal_handler);

	afrd_run ();

	afrd_fini ();

	ret = 0;

leave:
	if (g_cfg)
		cfg_free (g_cfg);

	if (g_daemon)
		unlink (g_pidfile);

	return ret;
}
