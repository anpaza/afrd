/*
 * Automatic Framerate Daemon
 * for AMLogic S905/S912-based boxes.
 */

#include "afrd.h"
#include "uevent_filter.h"

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
const char *g_mode_path = NULL;
static const char *g_vdec_status = NULL;
int g_mode_switch_delay_on;
int g_mode_switch_delay_off;
int g_mode_switch_delay_retry;
int g_mode_prefer_exact;
int g_mode_use_fract;
int g_mode_blacklist_rates [10];
int g_mode_blacklist_rates_size;

static uevent_filter_t g_filter_frhint;
static uevent_filter_t g_filter_vdec;
static uevent_filter_t g_filter_hdmi;

/**
 * The one-shot timer used to switch display mode.
 * If we switch display mode instantly after we receive an uevent,
 * this will work but can lead to excessive display mode switches,
 * which means worse user experience. Thus, we delay display mode
 * switching by MODE_SWITCH_DELAY milliseconds. If we don't receive
 * more uevents during that time, we'll switch the mode. This will
 * allow to accumulate several mode switches into one 
 */
static mstime_t g_ost_switch;

/**
 * One more timer to delay querying current video mode after HDMI
 * link becomes active.
 */
static mstime_t g_ost_hdmi;

/**
 * What mode should we set when g_ost_switch expires
 */
static struct
{
	// true to restore original display mode, false to set from current movie
	bool restore;
	// desired refresh rate in hz (fixed-point 24.8) if known, or 0 if not known
	int hz;
	// the initial video mode (this mode is set when restore==true)
	display_mode_t orig_mode;
} g_state;

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

static bool rate_is_blacklisted (int rate)
{
	for (int i = 0; i < g_mode_blacklist_rates_size; i++)
		if (abs (g_mode_blacklist_rates [i] - rate) <= 1)
			return true;
	return false;
}

static void query_vdec ()
{
	if (!g_vdec_status)
		return;

	trace (2, "Querying movie parameters from vdec\n");

	FILE *vsf = fopen (g_vdec_status, "r");
	if (!vsf) {
		trace (1, "Failed to open %s\n", g_vdec_status);
		return;
	}

	// the values we're going to extract
	long fps = 0, frame_dur = 0;

	char line [200];
	while (fgets (line, sizeof (line), vsf)) {
		// Bionic sscanf sucks badly, so parse the string manually...
		char *cur = line;
		const char *attr, *val;

		cur += strspn (cur, " \t\r\n");
		attr = cur;
		while (*cur && (*cur != ':'))
			cur++;
		if (!*cur)
			continue;
		*cur = 0;
		strip_trailing_spaces (cur, attr);
		cur++;
		cur += strspn (cur, " \t\r\n");

		val = cur;
		cur = strchr (cur, 0);
		strip_trailing_spaces (cur, val);

		trace (3, "\tattr [%s] val [%s]\n", attr, val);

		if (strcmp (attr, "frame rate") == 0) {
			char *endp;
			fps = strtol (val, &endp, 10);
			endp += strspn (endp, " \t\r\n");
			if ((*endp != 0) && (strcmp (endp, "fps") != 0)) {
				trace (2, "\tgarbage at end of 'frame rate': [%s]\n", endp);
				fps = 0;
			}
		} else if (strcmp (attr, "frame dur") == 0) {
			char *endp;
			frame_dur = strtol (val, &endp, 10);
			if (*endp != 0) {
				trace (2, "\tgarbage at end of 'frame dur': [%s]\n", endp);
				frame_dur = 0;
			}
		}
	}

	fclose (vsf);

	// Prefer frame_dur over fps, but sometimes it's 0
	if (frame_dur)
		g_state.hz = (256*96000 + frame_dur / 2) / frame_dur;
	else if ((fps > 0) && (fps <= 1000)) {
		switch (fps) {
			case 23: g_state.hz = (2997 * 256 + 62) / 125; break;
			case 29: g_state.hz = (2997 * 256 + 50) / 100; break;
			case 59: g_state.hz = (5994 * 256 + 50) / 100; break;
			default: g_state.hz = fps << 8; break;
		}
	}
}

static void framerate_switch ()
{
	if (g_state.restore) {
		if (!g_state.orig_mode.name) {
			trace (1, "No saved display mode to restore\n");
			return;
		}

		display_mode_switch (&g_state.orig_mode);
		g_state.orig_mode.name = NULL;
		return;
	}

	// if we don't known movie refresh rate, ask vdec
	if (g_state.hz == 0) {
		query_vdec ();
		if (g_state.hz == 0) {
			// Cannot determine movie frame rate, retry later
			if (g_mode_switch_delay_retry)
				mstime_arm (&g_ost_switch, g_mode_switch_delay_retry);
			return;
		}
	}

	// use fractional or integer frame rates if user requested so
	if (g_mode_use_fract != 0) {
		display_mode_t tmp;
		tmp.framerate = (g_state.hz + 0x80) >> 8;
		tmp.fractional = (g_mode_use_fract == 1);
		g_state.hz = display_mode_hz (&tmp);
	}

	display_mode_get_current ();

	trace (1, "Current mode is "DISPMODE_FMT"\n",
		DISPMODE_ARGS (g_current_mode, display_mode_hz (&g_current_mode)));
	trace (1, "Looking for display mode closest to %dx%d@%u.%02uHz\n",
		g_current_mode.width, g_current_mode.height,
		g_state.hz >> 8, (100 * (g_state.hz & 255)) >> 8);

	/* Find the video mode that:
	 * a) Has same width and height and interlace flag
	 * b) Closely divides by the required framerate, error less
	 *    than 4.1% (difference between 23.976 and 25 Hz)
	 * c) Has the highest framerate (e.g. 50Hz display modes are
	 *    better than 25Hz display modes for displaying 25Hz video)
	 */
	display_mode_t best_mode;
	unsigned best_rating = 0;
	int i;
	best_mode.name = NULL;
	for (i = 0; i < g_modes_n; i++) {
		display_mode_t *mode = &g_modes [i];
		if ((mode->width != g_current_mode.width) ||
		    (mode->height != g_current_mode.height) ||
		    (mode->interlaced != g_current_mode.interlaced))
			continue;

		int rating = 0;
		unsigned rate_n = 1;
		unsigned rate = (mode->framerate << 16) / g_state.hz;
		while (rate > 0x180) {
			rate_n++;
			rate = (mode->framerate << 16) / (g_state.hz * rate_n);
		}

		if (g_mode_prefer_exact) {
			unsigned n = (rate_n > 4) ? 3 : (rate_n - 1);
			rating += 8 * (3 - n);
		} else
			rating += 8 * (rate_n - 1);

		unsigned delta = abs ((int)(rate - 0x100));
		if (delta > 11)
			continue; // freq error > 4.3%

		rating += (21 - delta) * 64;

		if (rating > best_rating) {
			display_mode_t tmp = *mode;
			// decide if integer or fractional framerate is better
			display_mode_set_hz (&best_mode, g_state.hz);

			// if framerate is blacklisted, try to invert fractional
			if (rate_is_blacklisted (display_mode_hz (&tmp))) {
				tmp.fractional = !tmp.fractional;
				if (rate_is_blacklisted (display_mode_hz (&tmp)))
					// no luck, both framerates are banned
					continue;
			}

			best_rating = rating;
			best_mode = tmp;
		}
	}

	if (!best_mode.name) {
		trace (1, "Failed to find a suitable display mode\n");
		return;
	}

	// remember current video mode to restore it later
	if (!g_state.orig_mode.name &&
	    !display_mode_equal (&best_mode, &g_current_mode))
		g_state.orig_mode = g_current_mode;

	display_mode_switch (&best_mode);
}

/* @param restore true to delay restoring refresh rate to original,
 *      false to set refresh rate to match currently playing movie.
 * @param hz screen refresh rate in fixed-point 24.8 format if known,
 *      or 0 to ask movie refresh rate from vdec.
 */
static void delayed_framerate_switch (bool restore, int hz)
{
	mstime_t delay = restore ? g_mode_switch_delay_off : g_mode_switch_delay_on;

	trace (2, "Delaying switch to %u.%02u Hz by %d ms\n",
		hz >> 8, (100 * (hz & 255)) >> 8, delay);

	if (g_state.restore != restore) {
		g_state.restore = restore;
		g_state.hz = hz;
	}
	// if we known desired hz, don't overwrite with 0 that may come from vdec uevent
	if (hz)
		g_state.hz = hz;

	mstime_arm (&g_ost_switch, delay);
}

static void handle_hdmi_switch ()
{
	int hs = sysfs_get_int (g_hdmi_state, NULL);
	if (hs <= 0) {
		trace (1, "HDMI not active, clearing video mode list\n");
		display_modes_fini ();
		memset (&g_state.orig_mode, 0, sizeof (g_state.orig_mode));
	} else
		display_modes_init ();
}

static void handle_uevent (char *msg, ssize_t size)
{
	trace (2, "Parsing uevent\n");

	const char *frame_rate_hint = NULL;
	const char *frame_rate_end_hint = NULL;
	const char *action = NULL;

	uevent_filter_reset (&g_filter_frhint);
	uevent_filter_reset (&g_filter_vdec);
	uevent_filter_reset (&g_filter_hdmi);

	for (const char *end = msg + size; msg < end; msg += strlen (msg) + 1) {
		trace (3, "\t> %s\n", msg);

		char *val = strchr (msg, '=');
		if (val)
			*val++ = 0;
		else
			val = strchr (msg, 0);

		/* look for keywords we are interested */
		if (strcmp (msg, "FRAME_RATE_HINT") == 0)
			frame_rate_hint = val;
		else if (strcmp (msg, "FRAME_RATE_END_HINT") == 0)
			frame_rate_end_hint = val;
		else if (strcmp (msg, "ACTION") == 0)
			action = val;

		/* and count matches for every kind of uevent */
		uevent_filter_match (&g_filter_frhint, msg, val);
		uevent_filter_match (&g_filter_vdec, msg, val);
		uevent_filter_match (&g_filter_hdmi, msg, val);
		msg = val;
	}

	if (uevent_filter_matched (&g_filter_frhint)) {
		/* got a framerate hint uevent */
		if (frame_rate_hint) {
			char *end;
			int frh = strtoul (frame_rate_hint, &end, 10);
			if (frh && (!end || !*end)) {
				int fr_hz = (256*96000 + frh / 2) / frh;
				delayed_framerate_switch (false, fr_hz);
			}
		} else if (frame_rate_end_hint)
			delayed_framerate_switch (true, 0);

	} else if (uevent_filter_matched (&g_filter_vdec)) {
		/* got a vdec uevent */
		if (action && (strcmp (action, "add") == 0))
			delayed_framerate_switch (false, 0);
		else if (action && (strcmp (action, "remove") == 0))
			delayed_framerate_switch (true, 0);

	} else if (uevent_filter_matched (&g_filter_hdmi)) {
		/* hdmi plugged on or off */
		trace (1, "HDMI state changed, will handle in %d ms\n",
			DEFAULT_HDMI_DELAY);
		mstime_arm (&g_ost_hdmi, DEFAULT_HDMI_DELAY);

	} else
		trace (2, "\tUnrecognized uevent\n");
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

static void blacklist_rates_load (const char *kw)
{
	g_mode_blacklist_rates_size = 0;

	const char *str = cfg_get_str (kw, NULL);
	if (!str)
		return;

	trace (1, "\tloading blacklisted rates\n");

	char *tmp = strdup (str);
	char *cur = tmp;
	while (*cur) {
		cur += strspn (cur, spaces);
		char *next = cur + strcspn (cur, spaces);
		if (*next)
			*next++ = 0;

		float rate;
		if ((sscanf (cur, "%f", &rate) == 1) && (rate >= 1) && (rate <= 1000) &&
		    (g_mode_blacklist_rates_size < ARRAY_SIZE (g_mode_blacklist_rates))) {
			int irate = (int)(256.0 * rate + 0.5);
			g_mode_blacklist_rates [g_mode_blacklist_rates_size] = irate;
			g_mode_blacklist_rates_size++;
			trace (2, "\t+ %u.%02uHz\n", irate >> 8, (100 * (irate & 255)) >> 8);
		}

		cur = next;
	}

	free (tmp);
}

/* --------- * --------- * --------- * --------- * --------- * --------- */

int afrd_init ()
{
	trace (1, "afrd is initializing\n");

	g_hdmi_dev = cfg_get_str ("hdmi.dev", DEFAULT_HDMI_DEV);
	g_hdmi_state = cfg_get_str ("hdmi.state", DEFAULT_HDMI_STATE);

	g_mode_path = cfg_get_str ("mode.path", DEFAULT_VIDEO_MODE);
	g_mode_prefer_exact = cfg_get_int ("mode.prefer.exact", DEFAULT_MODE_PREFER_EXACT);
	g_mode_use_fract = cfg_get_int ("mode.use.fract", DEFAULT_MODE_USE_FRACT);
	blacklist_rates_load ("mode.blacklist.rates");

	g_mode_switch_delay_on = cfg_get_int ("switch.delay.on", DEFAULT_MODE_SWITCH_DELAY_ON);
	g_mode_switch_delay_off = cfg_get_int ("switch.delay.off", DEFAULT_MODE_SWITCH_DELAY_OFF);
	g_mode_switch_delay_retry = cfg_get_int ("switch.delay.retry", DEFAULT_MODE_SWITCH_DELAY_RETRY);

	g_vdec_status = cfg_get_str ("vdec.status", DEFAULT_VDEC_STATUS);
	uevent_filter_load (&g_filter_frhint, "uevent.filter.frhint");
	uevent_filter_load (&g_filter_vdec, "uevent.filter.vdec");
	uevent_filter_load (&g_filter_hdmi, "uevent.filter.hdmi");

	if (!uevent_open (16 * 1024)) {
		fprintf (stderr, "%s: failed to open uevent socket", g_program);
		return EPERM;
	}

	display_modes_init ();

	return 0;
}

void afrd_run ()
{
	if (g_uevent_sock == -1)
		return;

	trace (1, "afrd running\n");

	struct pollfd pfd;
	pfd.events = POLLIN;
	pfd.fd = g_uevent_sock;

	mstime_disable (&g_ost_switch);

	while (!g_shutdown) {
		int to = mstime_left (&g_ost_switch);
		int to2 = mstime_left (&g_ost_hdmi);
		if ((to < 0) || ((to2 >= 0) && (to2 < to)))
			to = to2;

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
		if (mstime_enabled (&g_ost_switch) &&
		    mstime_expired (&g_ost_switch)) {
			mstime_disable (&g_ost_switch);
			framerate_switch ();
		}

		if (mstime_enabled (&g_ost_hdmi) &&
		    mstime_expired (&g_ost_hdmi)) {
			mstime_disable (&g_ost_hdmi);
			handle_hdmi_switch ();
		}
	}

	// restore framerate just in case
	g_state.restore = true;
	framerate_switch ();
}

void afrd_fini ()
{
	if (g_uevent_sock != -1) {
		close (g_uevent_sock);
		g_uevent_sock = -1;
	}

	uevent_filter_fini (&g_filter_frhint);
	uevent_filter_fini (&g_filter_vdec);

	display_modes_fini ();
}
