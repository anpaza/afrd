/*
 * Automatic Framerate Daemon
 * for AMLogic S905/S912-based boxes.
 */

#include "afrd.h"

#define __USE_GNU
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <linux/netlink.h>
#include <sys/socket.h>

#ifndef SOCK_CLOEXEC
#  define SOCK_CLOEXEC O_CLOEXEC
#endif

#define CMSG_FOREACH(cmsg, mh)                                          \
	for ((cmsg) = CMSG_FIRSTHDR(mh); (cmsg); (cmsg) = CMSG_NXTHDR((mh), (cmsg)))

const char *g_hdmi_dev = NULL;
const char *g_hdmi_state = NULL;
static int g_uevent_sock = -1;
const char *g_video_mode = NULL;
int g_mode_switch_delay_on;
int g_mode_switch_delay_off;

/**
 * The one-shot timer used to switch display mode.
 * If we switch display mode instantly after we receive an uevent,
 * this will work but can lead to excessive display mode switches,
 * which means worse user experience. Thus, we delay display mode
 * switching by MODE_SWITCH_DELAY milliseconds. If we don't receive
 * more uevents during that time, we'll switch the mode. This will
 * allow to accumulate several mode switches into one 
 */
static mstime_t ost_switch;
static int need_hz;

static bool uevent_open (int buf_sz)
{
	struct sockaddr_nl addr;

	memset (&addr, 0, sizeof (addr));
	addr.nl_family = AF_NETLINK;
	addr.nl_pid = getpid ();
	addr.nl_groups = 0xffffffff;

	g_uevent_sock = socket (PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if (g_uevent_sock < 0)
		return false;

	setsockopt (g_uevent_sock, SOL_SOCKET, SO_RCVBUFFORCE, &buf_sz, sizeof (buf_sz));

	int one = 1;
	setsockopt (g_uevent_sock, SOL_SOCKET, SO_PASSCRED, &one, sizeof (one));

	if (bind (g_uevent_sock, (struct sockaddr *)&addr, sizeof (addr)) < 0) {
		close (g_uevent_sock);
		g_uevent_sock = -1;
		return false;
	}

	fcntl (g_uevent_sock, F_SETFL, O_NONBLOCK);

	return true;
}

// hz is framerate in Hertz, 24.8 fixed-point format
static void framerate_switch (unsigned hz)
{
	static unsigned old_hz;

	if (hz)
		trace (1, "Switching framerate to %u.%02u Hz\n", hz >> 8, (100 * (hz & 255)) >> 8);
	else {
		if (old_hz == 0)
			return;
		trace (1, "Restoring old framerate (%u hz)\n", old_hz);
	}

	int hs = sysfs_get_int (g_hdmi_state, NULL);
	if (hs <= 0) {
		trace (1, "HDMI not active, failing\n");

		// forget saved framerate and quit
		old_hz = 0;
		return;
	}

	if (display_mode_init () != 0)
		return;

	unsigned target_hz;
	if (hz == 0) {
		if (old_hz == 0) {
			trace (1, "Don't know what framerate to restore\n");
			return;
		}

		target_hz = old_hz << 8;
		old_hz = 0;
	} else
		target_hz = hz;

	trace (1, "Searching best match display mode for %u x %u @ %u.%02u Hz%s\n",
		g_current_mode.width, g_current_mode.height,
		target_hz >> 8, (100 * (target_hz & 255)) >> 8,
		g_current_mode.interlaced ? ", interlaced" : "");

	/* Find the video mode that:
	 * a) Has same width and height and interlace flag
	 * b) Closely divides by the required framerate, error less than 1%
	 * c) Has the highest framerate (e.g. 50Hz display modes are
	 *    better than 25Hz display modes for displaying 25Hz video)
	 */
	display_mode_t *best_mode = NULL;
	unsigned best_rating = 0;
	int i;
	for (i = 0; i < g_modes_n; i++) {
		display_mode_t *mode = &g_modes [i];
		if ((mode->width != g_current_mode.width) ||
		    (mode->height != g_current_mode.height) ||
		    (mode->interlaced != g_current_mode.interlaced))
			continue;

		unsigned rating = 0;
		unsigned rate = (mode->framerate << 16) / target_hz;
		while (rate > 0x180) {
			rate >>= 1;
			rating += 8;
		}

		unsigned delta = abs ((int)(rate - 0x100));
		if (delta >= 3)
			continue; // freq error > 1%

		rating += (2 - delta) * 64;

		if (rating > best_rating) {
			best_rating = rating;
			best_mode = mode;
		}
	}

	if (!best_mode) {
		trace (1, "Failed to find a suitable display mode\n");
		return;
	}

	if (display_mode_equal (best_mode, &g_current_mode)) {
		trace (1, "Current display mode is the best match\n");
		return;
	}

	trace (1, "Switching to best match display mode %u x %u @ %u Hz%s\n",
		best_mode->width, best_mode->height, best_mode->framerate,
		best_mode->interlaced ? ", interlaced" : "");

	if ((hz != 0) && (old_hz == 0))
		old_hz = g_current_mode.framerate;
	sysfs_write (g_video_mode, best_mode->name);
}

static void delayed_framerate_switch (unsigned hz)
{
	mstime_t delay = hz ? g_mode_switch_delay_on : g_mode_switch_delay_off;

	trace (2, "Delaying switch to %u.%02u Hz by %d ms\n",
		hz >> 8, (100 * (hz & 255)) >> 8, delay);

	need_hz = hz;
	mstime_arm (&ost_switch, delay);
}

static void handle_uevent (const char *msg, ssize_t size)
{
	static const char *frame_rate_hint;
	static const char *frame_rate_end_hint;
	typedef struct {
		const char *kw;
		const char *val;
		bool is_var;
	} kw_action_t;
	static const kw_action_t keywords [] = {
		{ "SUBSYSTEM", "tv" },
		{ "DEVNAME", "tv" },
		{ "ACTION", "change" },
		{ "FRAME_RATE_HINT", (const char *)&frame_rate_hint, true },
		{ "FRAME_RATE_END_HINT", (const char *)&frame_rate_end_hint, true },
	};

	frame_rate_hint = NULL;
	frame_rate_end_hint = NULL;
	for (const char *end = msg + size; msg < end; msg += strlen (msg) + 1) {
		const char *eq = strchr (msg, '=');
		if (!eq)
			eq = strchr (msg, 0);

		for (int i = 0; i < ARRAY_SIZE (keywords); i++) {
			size_t kwl = strlen (keywords [i].kw);
			if ((kwl == eq - msg) && !memcmp (keywords [i].kw, msg, kwl)) {
				if (*eq)
					eq++;
				if (keywords [i].is_var)
					*((const char **)keywords [i].val) = eq;
				else if (strcmp (keywords [i].val, eq))
					return;
			}
		}
	}

	if ((!frame_rate_hint && !frame_rate_end_hint) ||
	    (frame_rate_hint && frame_rate_end_hint))
		return;

	if (frame_rate_hint) {
		char *end;
		unsigned frh = strtoul (frame_rate_hint, &end, 10);
		if (frh && (!end || !*end)) {
			unsigned fr_hz = (256*96000 + frh / 2) / frh;
			delayed_framerate_switch (fr_hz);
		}
	} else if (frame_rate_end_hint)
		delayed_framerate_switch (0);
}

static void handle_uevents ()
{
	for (;;)
	{
		char msg [1024];
		struct sockaddr_nl addr;
		struct iovec iovec = {
			.iov_base = &msg,
			.iov_len = sizeof(msg),
		};
		union {
			struct cmsghdr cmsghdr;
			uint8_t buf [CMSG_SPACE (sizeof (struct ucred))];
		} control = {};
		struct msghdr msghdr = {
			.msg_name = &addr,
			.msg_namelen = sizeof (addr),
			.msg_iov = &iovec,
			.msg_iovlen = 1,
			.msg_control = &control,
			.msg_controllen = sizeof (control),
		};

		ssize_t size = recvmsg (g_uevent_sock, &msghdr, MSG_DONTWAIT);
		if (size < 0) {
			if (errno == EAGAIN)
				return;

			continue;
		}

		struct cmsghdr *cmsg;
		struct ucred *ucred = NULL;
		CMSG_FOREACH (cmsg, &msghdr) {
			if (cmsg->cmsg_level == SOL_SOCKET &&
			    cmsg->cmsg_type == SCM_CREDENTIALS &&
			    cmsg->cmsg_len == CMSG_LEN (sizeof (struct ucred)))
				ucred = (struct ucred*) CMSG_DATA (cmsg);
		}

		if (!ucred || ucred->pid != 0 || addr.nl_pid != 0)
			continue;

		handle_uevent (msg, size);
	}
}

/* --------- * --------- * --------- * --------- * --------- * --------- */

int afrd_init ()
{
	g_hdmi_dev = cfg_get_str ("hdmi.dev", DEFAULT_HDMI_DEV);
	g_hdmi_state = cfg_get_str ("hdmi.state", DEFAULT_HDMI_STATE);
	g_video_mode = cfg_get_str ("video.mode", DEFAULT_VIDEO_MODE);
	g_mode_switch_delay_on = cfg_get_int ("switch.delay.on", DEFAULT_MODE_SWITCH_DELAY_ON);
	g_mode_switch_delay_off = cfg_get_int ("switch.delay.off", DEFAULT_MODE_SWITCH_DELAY_OFF);

	if (!uevent_open (16 * 1024))
		return EPERM;

	return 0;
}

void afrd_run ()
{
	if (g_uevent_sock == -1)
		return;

	struct pollfd pfd;
	pfd.events = POLLIN;
	pfd.fd = g_uevent_sock;

	while (!g_shutdown) {
		int to = -1;
		if (mstime_enabled (&ost_switch))
			to = mstime_left (&ost_switch);

		// wait until either a new uevent comes
		// or the delayed mode switch timer expires
		pfd.revents = 0;
		int rc = poll (&pfd, 1, to);

		// update the millisecond timer
		mstime_update ();

		if (rc > 0)
			if (pfd.revents & POLLIN)
				handle_uevents ();

		// if mode switch timer expired, switch the mode finally
		if (mstime_enabled (&ost_switch) &&
		    mstime_expired (&ost_switch))
			framerate_switch (need_hz);
	}

	// restore framerate just in case
	framerate_switch (0);
}

void afrd_fini ()
{
	if (g_uevent_sock != -1) {
		close (g_uevent_sock);
		g_uevent_sock = -1;
	}
}
