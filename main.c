/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define __USE_GNU
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>

#include "afrd.h"

const char *g_version = "0.3.2";
const char *g_ver_sfx = "beta4";
const char *g_bdate = BDATE;
const char *g_config = "/etc/afrd.ini";
const char *g_pidfile =
#ifdef ANDROID
	// android has no standard directory for PID files...
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
static int g_logh = -1;
static bool g_logfn_firstuse = true;

// the global config
struct cfg_struct *g_cfg = NULL;

static void show_version ()
{
	printf ("afr daemon version %s built %s\n", g_version, g_bdate);
}

static void show_help (char *const *argv)
{
	show_version ();
	printf ("usage: %s [options] [config-file]\n", argv [0]);
	printf ("	-D	daemonize the program\n");
	printf ("	-p FILE	write PID to file when running as daemon\n");
	printf ("	-k	kill the running daemon (can be used with -D)\n");
	printf ("	-l FILE	write the log to FILE (imposes -vvv)\n");
	printf ("	-s	display running daemon stats\n");
	printf ("	-h	display this help\n");
	printf ("	-v	verbose info about what's cooking\n");
	printf ("	-V	display program version\n");
}

void trace_log (const char *logfn)
{
	if (g_logh >= 0) {
		close (g_logh);
		g_logh = -1;
	}

	if (!logfn)
		return;

	/* on first open rename old log to *~ */
	if (g_logfn_firstuse) {
		g_logfn_firstuse = false;
		char backup_logfn [300];
		snprintf (backup_logfn, sizeof (backup_logfn), "%s~", logfn);
		rename (logfn, backup_logfn);
	}

	g_logh = open (logfn, O_WRONLY | O_APPEND | O_CLOEXEC | O_CREAT /*| O_SYNC*/, 0644);
	if (g_logh < 0) {
		fprintf (stderr, "%s: failed to open log file %s", g_program, logfn);
		return;
	}
}

void trace_sync ()
{
	if (g_logh >= 0)
		fdatasync (g_logh);
}

void trace (int level, const char *format, ...)
{
	va_list argp;
	struct tm tm;
	struct timeval tv;
	bool write_logh = (g_logh >= 0) && (level <= 2);
	bool write_stdout = !g_daemon && (level <= g_verbose);

	if (!(write_logh || write_stdout))
		return;

	if (gettimeofday (&tv, NULL) < 0)
		return;

	localtime_r (&tv.tv_sec, &tm);

	char buff [1024];
	int n = snprintf (buff, sizeof (buff), "%02d:%02d:%02d.%03d ",
	                  tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(tv.tv_usec / 1000));

	va_start (argp, format);
	n += vsnprintf (buff + n, sizeof (buff) - n, format, argp);
	va_end (argp);

	if (write_logh)
		write (g_logh, buff, n);
	if (write_stdout)
		write (STDOUT_FILENO, buff, n);
}

static void signal_handler (int sig)
{
	g_shutdown = 1;
}

static void signal_emerg (int sig)
{
	// shit happened, just remove files and quit
	if (g_daemon)
		unlink (g_pidfile);
	afrd_emerg ();

	// restore original signal handler
	signal (sig, SIG_DFL);
}

int load_config (const char *config)
{
	int ret;

	trace (1, "loading config file '%s'\n", config);

	g_cfg = cfg_init ();

	if ((ret = cfg_load (g_cfg, config)) < 0) {
		cfg_free (g_cfg);
		g_cfg = NULL;
		trace (0, "failed to load config file '%s'\n", config);
		return ret;
	}

	trace (1, "\tsuccess\n");
	g_config = config;

	return 0;
}

// returns either the PID of running daemon, or
// -1 if PID file does not exist, or -2 if it
// exists but the contents are wrong.
static pid_t daemon_pid ()
{
	int h;
	pid_t pid = -1;

	h = open (g_pidfile, O_RDONLY);
	if (h >= 0) {
		int n;
		char tmp [11];

		pid = -2;
		n = read (h, tmp, sizeof (tmp) - 1);
		if (n > 0) {
			tmp [n] = 0;
			pid = strtoul (tmp, NULL, 0);
			// check if PID is valid
			if ((pid < 1) || kill (pid, 0))
				pid = -2;
		}

		close (h);
	}

	return pid;
}

static int kill_daemon ()
{
	pid_t pid = daemon_pid ();
	if (pid == -1) {
		fprintf (stderr, "%s: failed to read PID from file '%s'\n",
			g_program, g_pidfile);
	} else if (pid == -2) {
		unlink (g_pidfile);
		fprintf (stderr, "%s: PID file exists, but daemon is dead\n",
			g_program);
	} else if (kill (pid, SIGINT)) {
		fprintf (stderr, "%s: failed to kill daemon PID %d\n",
			g_program, pid);
	} else {
		for (int i = 0; i < 80; i++) {
			usleep (25000);
			if (kill (pid, 0) && (errno == ESRCH))
				break;
		}
		unlink (g_pidfile);
		return EXIT_SUCCESS;
	}

	return EXIT_FAILURE;
}

static void switch_namespace (int pid)
{
#ifdef ANDROID
#if __ANDROID_API__ >= 21
	char tmp [64];
	int h;

	snprintf (tmp, sizeof (tmp), "/proc/%d/ns/cgroup", pid);
	h = open (tmp, O_RDONLY);
	if (h >= 0) {
		int rc = setns (h, CLONE_NEWCGROUP);
		close (h);
	}

	snprintf (tmp, sizeof (tmp), "/proc/%d/ns/mnt", pid);
	h = open (tmp, O_RDONLY);
	if (h >= 0) {
		int rc = setns (h, CLONE_NEWNS);
		close (h);
	}
#else
#error "afrd is not designed to run on Android API level < 21"
#endif
#endif
}

static void daemonize ()
{
	/* switch to root namespace */
	switch_namespace (1);

	/* check PID */
	pid_t pid = daemon_pid ();
	if (pid > 0) {
		fprintf (stderr, "%s: daemon is already running with PID %d\n",
			g_program, pid);
		exit (EXIT_FAILURE);
	}

	pid = fork ();
	if (pid < 0) {
		fprintf (stderr, "%s: can't demonize, aborting\n", g_program);
		exit (EXIT_FAILURE);
	}

	if (pid != 0) {
		/* if directory for pid file does not exist, try to create it */
		char *pidfile = strdup (g_pidfile);
		char *dn = dirname (pidfile);
		if (*dn && (access (dn, F_OK) != 0))
			mkdir (dn, 0755);
		else
			// ensure sane access rights to /dev/run
			chmod (dn, 0755);
		free (pidfile);

		int h = open (g_pidfile, O_CREAT | O_WRONLY | O_CLOEXEC, 0644);
		if (h >= 0) {
			char tmp [10];
			write (h, tmp, snprintf (tmp, sizeof (tmp), "%d", pid));
			close (h);
		} else
			fprintf (stderr, "%s: failed to write PID file '%s'\n",
				g_program, g_pidfile);

		/* successfuly daemonized */
		exit (EXIT_SUCCESS);
	}

	/* continuing in child, close console */
	close (0);
	close (1);
	close (2);

	/* set priority a bit higher than normal -
	   we're kind of time-critical process */
	setpriority (PRIO_PROCESS, getpid (), -16);
}

static void display_stats ()
{
	if (!shmem_init (true))
		return;

	if (!shmem_read ()) {
		trace (0, "failed to read shared memory\n");
	} else {
		printf ("afrd version: %d.%d.%d built %s\n",
			g_afrd_stats.ver_major, g_afrd_stats.ver_minor, g_afrd_stats.ver_micro,
			g_afrd_stats.bdate);
		printf ("afrd is enabled: %s\n", g_afrd_stats.enabled ? "yes" : "no");
		printf ("Display refresh rate is switched: %s\n", g_afrd_stats.switched ? "yes" : "no");
		printf ("Display is blackened: %s\n", g_afrd_stats.blackened ? "yes" : "no");
		printf ("Current display refresh rate: %u.%02uHz\n",
			g_afrd_stats.current_hz >> 8, (100 * (g_afrd_stats.current_hz & 255)) >> 8);
		printf ("Original display refresh rate: %u.%02uHz\n",
			g_afrd_stats.original_hz >> 8, (100 * (g_afrd_stats.original_hz & 255)) >> 8);
	}

	shmem_fini ();
}

int main (int argc, char *const *argv)
{
	int ret;

	// ensure a sane umask so that user processes can read our files
	umask (022);

	g_program = argv [0];

	while ((ret = getopt (argc, argv, "Dp:kl:shvV")) >= 0)
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

			case 'l':
				trace_log (optarg);
				break;

			case 's':
				display_stats ();
				return 0;

			case 'v':
				g_verbose++;
				break;

			case 'h':
				show_help (argv);
				return EXIT_FAILURE;

			case 'V':
				show_version ();
				return EXIT_FAILURE;
		}

	if (g_daemon)
		// switch to root namespace
		switch_namespace (1);

	if (g_kill_daemon) {
		ret = kill_daemon ();
		if (!g_daemon)
			return ret;
	}

	if (g_daemon)
		daemonize ();

	// load the config files mentioned on command line, until one loads
	while (optind < argc)
		if ((ret = load_config (argv [optind++])) == 0)
			break;

	signal (SIGHUP, SIG_IGN);
	signal (SIGINT, signal_handler);
	signal (SIGQUIT, signal_handler);
	signal (SIGTERM, signal_handler);

	signal (SIGFPE, signal_emerg);
	signal (SIGILL, signal_emerg);
	signal (SIGSEGV, signal_emerg);

	for (;;) {
		if ((ret = afrd_init ()) < 0)
			break;
		ret = afrd_run ();
		afrd_fini ();

		if (ret <= 0)
			break;
	}

	if (g_cfg)
		cfg_free (g_cfg);

	if (g_daemon)
		unlink (g_pidfile);

	return ret;
}
