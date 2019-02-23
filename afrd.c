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
int g_mode_switch_delay_retry;
static const char *g_vdec_status = NULL;
static char *g_uevent_filter_frhint = NULL;
static int g_uevent_filter_frhint_len = 0;
static char *g_uevent_filter_vdec = NULL;
static int g_uevent_filter_vdec_len = 0;

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

/**
 * What we need to do after ost_switch expires
 */
static struct
{
	// true to restore original refresh rate, false to set RR from current movie
	bool restore;
	// desired refresh rate in hz (fixed-point 24.8) if known, or 0 if not known
	int hz;
} need;

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

static void strip_trailing_spaces (char *eol, const char *start)
{
	while (eol > start) {
		eol--;
		if (strchr (" \t\r\n", *eol) == NULL) {
			eol [1] = 0;
			return;
		}
	}

	eol [0] = 0;
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

		trace (2, "\tattr [%s] val [%s]\n", attr, val);

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
		need.hz = (256*96000 + frame_dur / 2) / frame_dur;
	else if (fps) {
		switch (fps) {
			case 23: need.hz = (256 * 23.97); break;
			case 29: need.hz = (256 * 29.97); break;
			case 59: need.hz = (256 * 59.94); break;
			default: need.hz = 256 * fps; break;
		}
	}
}

static void framerate_switch ()
{
	static display_mode_t saved_mode;

	trace (1, "%s display mode\n", need.restore ? "Restoring" : "Setting");

	// check if TV is plugged in and on
	int hs = sysfs_get_int (g_hdmi_state, NULL);
	if (hs <= 0) {
		trace (1, "HDMI not active, failing\n");

		// forget saved framerate and quit
		memset (&saved_mode, 0, sizeof (saved_mode));
		return;
	}

	if (need.restore) {
		if (!saved_mode.name) {
			trace (1, "No saved display mode to restore\n");
			return;
		}

		display_mode_switch (&saved_mode);
		saved_mode.name = NULL;
		return;
	}

	// if we don't known movie refresh rate, ask vdec
	if (need.hz == 0) {
		query_vdec ();
		if (need.hz == 0) {
			// Cannot determine movie frame rate, retry later
			if (g_mode_switch_delay_retry)
				mstime_arm (&ost_switch, g_mode_switch_delay_retry);
			return;
		}
	}

	if (display_modes_init () != 0)
		return;

	trace (1, "Searching display mode similar to %s at %u.%02uHz\n",
		g_current_mode.name, need.hz >> 8, (100 * (need.hz & 255)) >> 8);

	/* Find the video mode that:
	 * a) Has same width and height and interlace flag
	 * b) Closely divides by the required framerate, error less than 1%
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

		unsigned rating = 0;
		unsigned rate_n = 1;
		unsigned rate = (mode->framerate << 16) / need.hz;
		while (rate > 0x180) {
			rate_n++;
			rate = (mode->framerate << 16) / (need.hz * rate_n);
			rating += 8;
		}

		unsigned delta = abs ((int)(rate - 0x100));
		if (delta > 3)
			continue; // freq error > 1.17%

		rating += (2 - delta) * 64;

		if (rating > best_rating) {
			best_rating = rating;
			best_mode = *mode;
			// decide if integer or fractional framerate is better
			display_mode_set_hz (&best_mode, need.hz);
		}
	}

	if (!best_mode.name) {
		trace (1, "Failed to find a suitable display mode\n");
		return;
	}

	if (display_mode_equal (&best_mode, &g_current_mode)) {
		trace (1, "Current display mode is the best match\n");
		return;
	}

	// remember current video mode to restore it later
	if ((need.hz != 0) && (!saved_mode.name))
		saved_mode = g_current_mode;
	display_mode_switch (&best_mode);
}

/* @param restore true to delay restoring refresh rate to original,
 *      false to set refresh rate to match currently playing movie.
 * @param hz screen refresh rate in fixed-point 24.8 format if known,
 *      or 0 to ask movie refresh rate from vdec.
 */
static void delayed_framerate_switch (bool restore, unsigned hz)
{
	mstime_t delay = restore ? g_mode_switch_delay_off : g_mode_switch_delay_on;

	trace (2, "Delaying switch to %u.%02u Hz by %d ms\n",
		hz >> 8, (100 * (hz & 255)) >> 8, delay);

	if (need.restore != restore) {
		need.restore = restore;
		need.hz = hz;
	}
	// if we known desired hz, don't overwrite with 0 that may come from vdec uevent
	if (hz)
		need.hz = hz;

	mstime_arm (&ost_switch, delay);
}

static bool uevent_filter_match (const char *kw, int kwlen, const char *val,
	const char *filter, int filter_count)
{
	size_t valen = strlen (val);
	const char *fkw = filter;
	while (filter_count--) {
		const char *fval = strchr (fkw, '=');
		if (!fval)
			fval = strchr (fkw, 0);

		if ((kwlen == (fval - fkw)) &&
		    (memcmp (kw, fkw, kwlen) == 0)) {
			/* keyword matched, now check value */
			if (*fval)
				fval++;
			size_t fvalen = strlen (fval);
			/* allow filter value to specify just the prefix of attr value */
			if ((fvalen <= valen) &&
			    (memcmp (val, fval, fvalen) == 0))
				return true;

			/* keywords never repeat in a single uevent, so we fail */
			return false;
		}

		fkw = strchr (fval, 0) + 1;
	}

	return false;
}

static void handle_uevent (const char *msg, ssize_t size)
{
	trace (2, "Parsing uevent\n");

	const char *frame_rate_hint = NULL;
	const char *frame_rate_end_hint = NULL;
	const char *action = NULL;

	// decide which of the two event kinds we handle is this
	int uev_frhint_len = 0;
	int uev_vdec_len = 0;

	for (const char *end = msg + size; msg < end; msg += strlen (msg) + 1) {
		trace (2, "\t> %s\n", msg);

		const char *val = strchr (msg, '=');
		if (!val)
			val = strchr (msg, 0);
		int skwl = val - msg;
		if (*val)
			val++;

		/* look for keywords we are interested */
		if ((skwl == 15) &&
		    !memcmp (msg, "FRAME_RATE_HINT", 15))
			frame_rate_hint = val;
		else if ((skwl == 19) &&
		    !memcmp (msg, "FRAME_RATE_END_HINT", 19))
			frame_rate_end_hint = val;
		else if ((skwl == 6) &&
		    !memcmp (msg, "ACTION", 6))
			action = val;

		/* and count matches for every kind of uevent */
		if (uevent_filter_match (msg, skwl, val,
			g_uevent_filter_frhint, g_uevent_filter_frhint_len))
			uev_frhint_len++;
		if (uevent_filter_match (msg, skwl, val,
			g_uevent_filter_vdec, g_uevent_filter_vdec_len))
			uev_vdec_len++;
	}

	if ((uev_frhint_len == g_uevent_filter_frhint_len) &&
	    (!frame_rate_hint == !frame_rate_end_hint)) {
		/* got a framerate hint uevent */
		if (frame_rate_hint) {
			char *end;
			unsigned frh = strtoul (frame_rate_hint, &end, 10);
			if (frh && (!end || !*end)) {
				unsigned fr_hz = (256*96000 + frh / 2) / frh;
				delayed_framerate_switch (false, fr_hz);
			}
		} else if (frame_rate_end_hint)
			delayed_framerate_switch (true, 0);

	} else if ((uev_vdec_len == g_uevent_filter_vdec_len) &&
	    (action != NULL)) {
		/* got a vdec uevent */
		trace (0, " - got vdec event [%s] -\n", action);
		if (strcmp (action, "add") == 0)
			delayed_framerate_switch (false, 0);
		else if (strcmp (action, "remove") == 0)
			delayed_framerate_switch (true, 0);
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

/* --------- * --------- * --------- * --------- * --------- * --------- */

static int load_uevent_filter (const char *kw, char **filter)
{
	const char *val = cfg_get_str (kw, NULL);
	if (!val)
		return 0;

	int count = 0;
	size_t val_len = strlen (val);
	char *dst = (*filter) = malloc (val_len + 1);
	bool skip_spaces = true;
	char *last_kw = (*filter);
	while (*val) {
		switch (*val) {
			case ',':
				/* remove trailing spaces */
				while ((dst > last_kw) && strchr (" \t", dst [-1]))
					dst--;
				*dst++ = 0;

				count++;
				trace (1, "\t+ %s: %s\n", kw, last_kw);

				last_kw = dst;
				skip_spaces = true;
				break;

			case ' ':
			case '\t':
				/* remove leading spaces */
				if (skip_spaces)
					break;
				*dst++ = *val;
				break;

			default:
				skip_spaces = false;
				*dst++ = *val;
				break;
		}
		val++;
	}

	/* remove trailing spaces from last keyword */
	while ((dst > last_kw) && strchr (" \t", dst [-1]))
		dst--;
	*dst++ = 0;

	count++;
	trace (1, "\t+ %s: %s\n", kw, last_kw);

	return count;
}

int afrd_init ()
{
	int i;

	trace (1, "afrd is initializing\n");

	g_hdmi_dev = cfg_get_str ("hdmi.dev", DEFAULT_HDMI_DEV);
	g_hdmi_state = cfg_get_str ("hdmi.state", DEFAULT_HDMI_STATE);
	g_video_mode = cfg_get_str ("video.mode", DEFAULT_VIDEO_MODE);
	g_mode_switch_delay_on = cfg_get_int ("switch.delay.on", DEFAULT_MODE_SWITCH_DELAY_ON);
	g_mode_switch_delay_off = cfg_get_int ("switch.delay.off", DEFAULT_MODE_SWITCH_DELAY_OFF);
	g_mode_switch_delay_retry = cfg_get_int ("switch.delay.retry", DEFAULT_MODE_SWITCH_DELAY_RETRY);
	g_vdec_status = cfg_get_str ("vdec.status", DEFAULT_VDEC_STATUS);

	g_uevent_filter_frhint_len =
		load_uevent_filter ("uevent.filter.frhint", &g_uevent_filter_frhint);
	g_uevent_filter_vdec_len =
		load_uevent_filter ("uevent.filter.vdec", &g_uevent_filter_vdec);

	if (!uevent_open (16 * 1024)) {
		fprintf (stderr, "%s: failed to open uevent socket", g_program);
		return EPERM;
	}

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

	mstime_disable (&ost_switch);

	while (!g_shutdown) {
		int to = mstime_left (&ost_switch);

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
		    mstime_expired (&ost_switch)) {
			mstime_disable (&ost_switch);
			framerate_switch ();
		}
	}

	// restore framerate just in case
	need.restore = true;
	framerate_switch ();
}

void afrd_fini ()
{
	if (g_uevent_sock != -1) {
		close (g_uevent_sock);
		g_uevent_sock = -1;
	}

	if (g_uevent_filter_frhint) {
		free (g_uevent_filter_frhint);
		g_uevent_filter_frhint = NULL;
	}
	if (g_uevent_filter_vdec) {
		free (g_uevent_filter_vdec);
		g_uevent_filter_vdec = NULL;
	}

	display_modes_finit ();
}
