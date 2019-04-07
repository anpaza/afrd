/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * afrd API through localhost:50505
 */

#include "afrd.h"

#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>

static int g_apisock = -1;

bool apisock_init ()
{
	g_apisock = socket (AF_INET, SOCK_DGRAM, 0);
	if (g_apisock == -1) {
		trace (0, "Failed to create socket\n");
		return false;
	}

	int opt = 1;
	setsockopt (g_apisock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof (opt));

	struct sockaddr_in addr;
	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	addr.sin_port = htons (AFRD_API_PORT);
	if (bind (g_apisock, (const struct sockaddr *)&addr, sizeof (addr)) < 0) {
		trace (0, "Failed to bind socket to port %d\n", AFRD_API_PORT);
		apisock_fini ();
		return false;
	}

	//fcntl (g_apisock, F_SETFL, O_NONBLOCK);

	trace (1, "AFRd API available at 127.0.0.1:%d UDP\n", AFRD_API_PORT);

	return true;
}

void apisock_fini ()
{
	if (g_apisock != -1) {
		close (g_apisock);
		g_apisock = -1;
	}
}

int apisock_prep_poll (struct pollfd *pfd, int pfd_count)
{
	if (g_apisock == -1)
		if (!apisock_init ())
			return 0;

	int n = 0;

	if (pfd_count) {
		pfd->fd = g_apisock;
		pfd->events = POLLIN;
		pfd->revents = 0;
		pfd_count--;
		pfd++;
		n++;
	}

	return n;
}

static bool apisock_is_cmd (char **cmd, const char *kw)
{
	char *cur = *cmd;
	int kwl = strlen (kw);
	if (strncmp (cur, kw, kwl))
		return false;

	int space = cur [kwl];
	if (space && !strchr (spaces, space))
		return false;

	cur += kwl;
	cur += strspn (cur, spaces);
	*cmd = cur;
	return true;
}

static void apisock_cmd (char *cmd, int fd, struct sockaddr *src_addr, socklen_t addrlen)
{
	while (*cmd) {
		cmd += strspn (cmd, spaces);
		char *eol = strchr (cmd, '\n');
		char *next;
		if (eol)
			next = eol + 1;
		else
			next = eol = strchr (cmd, 0);

		strip_trailing_spaces (eol, cmd);
		trace (2, "API command: [%s]\n", cmd);

		if (apisock_is_cmd (&cmd, "help")) {
			static const char *help =
				"help\n\tdisplay this help text\n"
				"frame_rate_hint <fr>\n\ttell afrd the video starting in <1.0 seconds will use <fr>/1000 frames per second (e.g. 23976 = 23.976 fps)\n"
				"refresh_rate <rr>\n\ttell afrd to set display refresh rate as close to <rr>/1000 Hz as possible, no arg to restore original rate\n"
				"color_space <cs>\n\toverride colorspace, empty arg to restore default behavior\n"
				"status\n\tget current afrd status\n"
				"reconf\n\ttell afrd to reload configuration file as soon as possible\n";
			sendto (fd, help, strlen (help), 0, src_addr, addrlen);
		} else if (apisock_is_cmd (&cmd, "frame_rate_hint")) {
			int fr = parse_int (&cmd);
			cmd += strspn (cmd, spaces);
			if (!*cmd)
				afrd_frame_rate_hint ((fr * 256) / 1000);
		} else if (apisock_is_cmd (&cmd, "status")) {
			char status [200];
			int sl = snprintf (status, sizeof (status),
				"stamp:%d\n"
				"enabled:%d\n"
				"active:%d\n"
				"blackened:%d\n"
				"version:%d.%d.%d\n"
				"build:%s\n"
				"current hz:%d\n"
				"original hz:%d\n",
				g_afrd_stats.crc32,
				g_afrd_stats.enabled ? 1 : 0,
				g_afrd_stats.switched ? 1 : 0,
				g_afrd_stats.blackened ? 1 : 0,
				g_afrd_stats.ver_major, g_afrd_stats.ver_minor, g_afrd_stats.ver_micro,
				g_afrd_stats.bdate,
				g_afrd_stats.current_hz * 1000 / 256,
				g_afrd_stats.original_hz * 1000 / 256);
			sendto (fd, status, sl, 0, src_addr, addrlen);
		} else if (apisock_is_cmd (&cmd, "reconf")) {
			afrd_reconf ();
		} else if (apisock_is_cmd (&cmd, "refresh_rate")) {
			int fr = parse_int (&cmd);
			cmd += strspn (cmd, spaces);
			if (!*cmd)
				afrd_refresh_rate ((fr * 256) / 1000);
		} else if (apisock_is_cmd (&cmd, "color_space")) {
			afrd_override_colorspace (&cmd);
		} else {
			trace (2, "\t> unknown command\n");
			cmd = strchr (cmd, 0);
		}

		if (*cmd)
			trace (2, "\t> bad args\n");

		cmd = next;
	}
}

void apisock_handle (struct pollfd *pfd, int pfd_count)
{
	for (; pfd_count; pfd++, pfd_count--) {
		if (pfd->fd == g_apisock) {
			if (pfd->revents & (POLLHUP | POLLERR | POLLNVAL))
				apisock_fini ();
			else if (pfd->revents & POLLIN) {
				char cmd [1024];
				struct sockaddr src_addr;
				socklen_t addrlen = sizeof (src_addr);
				int n = recvfrom (g_apisock, cmd, sizeof (cmd) - 1, 0,
					          &src_addr, &addrlen);
				if (n > 0) {
					cmd [n] = 0;
					apisock_cmd (cmd, pfd->fd, &src_addr, addrlen);
				}
			}
		}
	}
}
