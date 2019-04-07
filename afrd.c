/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

#include "afrd.h"
#include "uevent_filter.h"
#include "colorspace.h"

#define __USE_GNU
#include <unistd.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/stat.h>

#ifndef SOCK_CLOEXEC
#  define SOCK_CLOEXEC O_CLOEXEC
#endif

#define CMSG_FOREACH(cmsg, mh)                                          \
	for ((cmsg) = CMSG_FIRSTHDR(mh); (cmsg); (cmsg) = CMSG_NXTHDR((mh), (cmsg)))

const char *g_hdmi_dev = NULL;
const char *g_mode_path = NULL;

static const char *g_hdmi_state = NULL;
static int g_uevent_sock = -1;
static const char *g_vdec_sysfs = NULL;

static int g_switch_delay_on;
static int g_switch_delay_off;
static int g_switch_delay_retry;
static int g_switch_timeout;
static int g_switch_blackout;
static int g_switch_ignore;
static int g_switch_hdmi;

static int g_mode_prefer_exact;
static int g_mode_use_fract;
static int g_mode_blacklist_rates [10];
static int g_mode_blacklist_rates_count;

static bool g_enable;                 // enabled in config file

static uevent_filter_t g_filter_frhint;
static uevent_filter_t g_filter_vdec;
static uevent_filter_t g_filter_hdmi;

static strlist_t g_vdec_blacklist;
static strlist_t g_frhint_vdec_blacklist;

/**
 * The one-shot timer used to switch display mode.
 * If we switch display mode instantly after we receive an uevent,
 * this will work but can lead to excessive display mode switches,
 * which means worse user experience. Thus, we delay display mode
 * switching by SWITCH_DELAY milliseconds. If we don't receive
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
 * Screen blackout timer
 */
static mstime_t g_ost_blackout;

/**
 * Timer for periodic config timestamp checks
 */
static mstime_t g_ost_config;

/**
 * Time stamp of the last refresh rate restore event
 */
static mstime_t g_ost_off;

/**
 * Available sources for fps values
 */
typedef enum
{
	// FRAME_RATE_HINT should be immediately usable
	SRC_FRH,
	// dump_vdec_chunks is more reliable than others, only 2 confirmations
	SRC_CHUNKS,
	// dump_vdec_blocks is less reliable, so 3 confirmations
	SRC_BLOCKS,
	// need 4 confirmations to accept vdec_status
	SRC_VDEC,

	// number of fps sources
	SRC_COUNT
} hz_source_t;

static const uint8_t src_weight [SRC_COUNT] =
{
	100,	// FRH
	50,	// CHUNKS
	34,	// BLOCKS
	25,	// VDEC
};

// accept hz when weight sum up to this
#define ACCEPT_HZ_WEIGHT        100

// minimum sane refresh rate, .8 fixed-point
#define HZ_MIN		FP8 ( 10,000)
#define HZ_MAX		FP8 (100,000)

typedef struct
{
	// last seen value from this source
	int hz;
	// total weight of hz samples
	int weight;
	// expiration timer for collecting data from this source
	mstime_t timeout;
} hz_stat_t;

/**
 * Frame rate detector data.
 */
static struct
{
	// true to restore original display mode, false to set from current movie
	bool restore;
	// desired refresh rate in hz (fixed-point 24.8) if known, or 0 if not known
	int hz;
	// the initial video mode (this mode is set when restore==true)
	display_mode_t orig_mode;
	// active video decoder driver name
	char modalias [32];
	// hz detection timeout
	mstime_t hz_ost;

	// we accumulate fps data from every source
	hz_stat_t hz_stat [SRC_COUNT];

	// a stamp to detect when dump_vdec_blocks stays still
	int hz_samples_stamp;
} g_state;

/**
 *
 */
static struct
{
	// time stamp when this hint was valid
	mstime_t stamp;
	// the declared movie fps
	int fps;
} g_frame_rate_hint;


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

static void update_stats ()
{
	g_afrd_stats.enabled = g_enable;
	g_afrd_stats.switched = (g_state.orig_mode.name [0] != 0);
	g_afrd_stats.blackened = g_blackened;
	g_afrd_stats.current_hz = display_mode_hz (&g_current_mode);
	g_afrd_stats.original_hz = display_mode_hz (g_afrd_stats.switched ?
		&g_state.orig_mode : &g_current_mode);
	shmem_update ();
}

static bool rate_is_blacklisted (int rate)
{
	for (int i = 0; i < g_mode_blacklist_rates_count; i++)
		if (abs (g_mode_blacklist_rates [i] - rate) <= 1)
			return true;
	return false;
}

// note! this function considers fractional rate "equal" to integer rate!
// it is a rough test to filter off data that is way off the mainline.
static bool hz_close (int hz1, int hz2)
{
	// if is +- 1 units, consider it a rounding error
	if (abs (hz1 - hz2) <= 1)
		return true;

	// if they are no more than 0.5% apart, it's a rounding error
	return abs (10000 - ((hz1 * 10000 + hz2 / 2) / hz2)) <= 50;
}

// round hz to nearest known standard framerate
static int hz_round (int hz)
{
	static short sane_hz [] =
	{
		FP8 (23,976), FP8 (24,000),
		FP8 (25,000),
		FP8 (29,970), FP8 (30,000),
		FP8 (50,000),
		FP8 (59,940), FP8 (60,000),
	};

	int closest_hz = 0;
	int closest_delta = 99999999;

	for (size_t i = 0; i < ARRAY_SIZE (sane_hz); i++) {
		int shz = sane_hz [i];
		int delta = abs (shz - hz);
		if (delta < closest_delta) {
			closest_delta = delta;
			closest_hz = shz;
		}
	}

	dtrace (3, "\t> hz "HZ_FMT" closest "HZ_FMT"\n", HZ_ARGS(hz), HZ_ARGS(closest_hz));

	// if frame rates are close enough, consider them equal
	if (hz_close (hz, closest_hz))
		return closest_hz;

	// otherwise, invalid frame rate
	return 0;
}

// accumulate fps data from different sources
static void accumulate_fps (int hz, hz_source_t src)
{
	hz_stat_t *stat = &g_state.hz_stat [src];
	if (stat->weight && !hz_close (hz, stat->hz)) {
		g_state.hz_stat [src].weight = 0;
		trace (2, "Resetting Hz weight\n");
	}

	int weight = src_weight [src];

	stat->hz = hz;
	stat->weight += weight;
	mstime_arm (&stat->timeout, g_switch_delay_retry * 2);

	trace (2, "Accumulating "HZ_FMT"fps src %d weight %d total %d\n",
		HZ_ARGS (hz), src, weight, stat->weight);
}

// guess the best fps from accumulated data, more insistent if last_chance is true
static int best_fps (bool last_chance)
{
	hz_stat_t *best_stat = NULL;
	int best_prio = 0;
	// if last chance, use any detection that we're at least half sure
	int accept_weight = last_chance ? ACCEPT_HZ_WEIGHT/2 : ACCEPT_HZ_WEIGHT;

	for (int i = 0; i < SRC_COUNT; i++) {
		if (best_prio > src_weight [i])
			continue;

		hz_stat_t *stat = &g_state.hz_stat [i];
		if (stat->weight == 0)
			continue;

		if (last_chance) {
			// last chance to detect fps
			dtrace (3, "last_chance src %d weight %d\n", i, stat->weight);
			if (stat->weight < accept_weight)
				continue;
		} else {
			// we still have time, wait best src that has a chance to finish
			if (!mstime_enabled (&stat->timeout) ||
			    mstime_expired (&stat->timeout))
				continue;
		}

		best_prio = src_weight [i];
		best_stat = stat;
	}

	if (best_stat)
		dtrace (3, "best "HZ_FMT"Hz weight %d timeout %d\n",
		        HZ_ARGS (best_stat->hz), best_stat->weight,
		        mstime_left (&best_stat->timeout));

	if (!best_stat || (best_stat->weight < accept_weight))
		return 0;

	return best_stat->hz;
}

static bool query_vdec_blocks ()
{
	if (!g_vdec_sysfs)
		return false;

	trace (2, "Querying vdec_blocks\n");

	char line [200];
	snprintf (line, sizeof (line), "%s/dump_vdec_blocks", g_vdec_sysfs);
	FILE *vsf = fopen (line, "r");
	if (!vsf)
		return false;

	bool ok = fgets (line, sizeof (line), vsf) != NULL;
	fclose (vsf);
	if (!ok)
		return false;

	unsigned dsize = find_ulong (line, ",dsize=", &ok);
	unsigned nframes = find_ulong (line, ",frames:", &ok);
	unsigned timeint = find_ulong (line, ",dur:", &ok);

	dtrace (3, "\t> dsize %u frames %u dur %u\n", dsize, nframes, timeint);

	// if we don't have enough stats, don't take it into account
	if (!ok || nframes < 5 || timeint < 120 ||
	    g_state.hz_samples_stamp == dsize)
		return false;

	g_state.hz_samples_stamp = dsize;
	int hz = hz_round ((nframes * 256000 + timeint / 2) / timeint);
	if (hz == 0)
		return false;

	trace (2, "\t%d frames played over last %dms at "HZ_FMT"fps\n",
		nframes, timeint, HZ_ARGS (hz));
	accumulate_fps (hz, SRC_BLOCKS);
	return true;
}

static int compare_pts (const void *a, const void *b)
{
	int ptsa = *(int *)a;
	int ptsb = *(int *)b;
	return ptsa - ptsb;
}

static bool query_vdec_chunks ()
{
	if (!g_vdec_sysfs)
		return false;

	trace (2, "Querying vdec_chunks\n");

	char buff [4096];
	snprintf (buff, sizeof (buff), "%s/dump_vdec_chunks", g_vdec_sysfs);
	int vsh = open (buff, O_RDONLY);
	if (vsh < 0)
		return false;

	int size = read (vsh, buff, sizeof (buff) - 1);
	close (vsh);
	if (size < 100)
		return false;

	buff [size] = 0;
	char *cur = buff;
	int pts [64];
	int pts_size = 0;
	unsigned long long pts64_base;

	while (*cur) {
		char *eol = strchr (cur, '\n');
		if (!eol)
			break; // incomplete line
		*eol = '\0';

		bool ok = true;
		unsigned long long pts64 = find_ulonglong (cur, "pts64=", &ok);
		if (ok) {
			if (!pts_size)
				pts64_base = pts64;
			pts [pts_size++] = (int)(pts64 - pts64_base);
			if (pts_size > ARRAY_SIZE (pts))
				break; // enough data
		}

		cur = eol + 1;
	}

	if (pts_size < 5)
		return false;

	qsort (pts, pts_size, sizeof (pts [0]), compare_pts);

	// transform to usecs per frame
	int i;
	for (i = 1; i < pts_size; i++)
		pts [i - 1] = pts [i] - pts [i - 1];
	pts_size--;

	// now compute the average frame rate
	int base_pts = pts [0];
	int avg_pts = base_pts;
	int avg_count = 1;
	for (i = 1; i < pts_size; i++) {
		int rate = 128 * pts [i] / base_pts;
		if ((rate >= 247) && (rate <= 264)) {
			// pts [i] is a frame skip
			avg_count++;
		} else if ((rate >= 62) && (rate <= 66)) {
			// all previous pts were frame skips
			avg_count *= 2;
			base_pts = pts [i];
		} else if ((pts [i] > base_pts + 1500) || (pts [i] < base_pts - 1500))
			continue; // totally wrong pts
		avg_count++;
		avg_pts += pts [i];
	}

	if (avg_count < 3)
		return false;

	dtrace (3, "\t> %d pts in %d us, base pts %d\n", avg_count, avg_pts, base_pts);

	int hz = hz_round ((avg_count * 256 * 1000) / (avg_pts / 1000));
	if (hz == 0)
		return false;

	// this gives fps with very high precision
	accumulate_fps (hz, SRC_CHUNKS);
	return true;
}

static bool query_vdec ()
{
	if (!g_vdec_sysfs)
		return false;

	trace (2, "Querying vdec_status\n");

	char line [200];
	snprintf (line, sizeof (line), "%s/vdec_status", g_vdec_sysfs);
	FILE *vsf = fopen (line, "r");
	if (!vsf) {
		trace (1, "Failed to open %s/vdec_status\n", g_vdec_sysfs);
		g_vdec_sysfs = NULL;
		return false;
	}

	// the values we're going to extract
	int fps = 0, frame_dur = 0;

	while (fgets (line, sizeof (line), vsf)) {
		// Bionic sscanf sucks badly, so parse the string manually...
		char *cur = line;
		const char *attr, *val;

		cur += strspn (cur, spaces);
		attr = cur;
		while (*cur && (*cur != ':'))
			cur++;
		if (!*cur)
			continue;
		*cur = 0;
		strip_trailing_spaces (cur, attr);
		cur++;
		cur += strspn (cur, spaces);

		val = cur;
		cur = strchr (cur, 0);
		strip_trailing_spaces (cur, val);

		dtrace (4, "\tattr [%s] val [%s]\n", attr, val);

		if (strcmp (attr, "frame rate") == 0) {
			char *endp;
			fps = strtol (val, &endp, 10);
			endp += strspn (endp, spaces);
			if ((*endp != 0) && (strcmp (endp, "fps") != 0)) {
				trace (2, "\tgarbage at end of 'frame rate': [%s]\n", endp);
				fps = 0;
			} else
				trace (3, "\t> frame rate %d\n", fps);
		} else if (strcmp (attr, "frame dur") == 0) {
			char *endp;
			frame_dur = strtol (val, &endp, 10);
			if (*endp != 0) {
				trace (2, "\tgarbage at end of 'frame dur': [%s]\n", endp);
				frame_dur = 0;
			} else
				trace (3, "\t> frame dur %d\n", frame_dur);
		}
	}

	fclose (vsf);

	// Prefer frame_dur over fps, but sometimes it's 0 and sometimes it's insane
	int hz = 0;
	if (frame_dur)
		hz = hz_round ((256*96000 + frame_dur / 2) / frame_dur);

	if ((hz == 0) && (fps != 0)) {
		switch (fps) {
			case 23: hz = (2997 * 256 + 62) / 125; break;
			case 29: hz = (2997 * 256 + 50) / 100; break;
			case 59: hz = (5994 * 256 + 50) / 100; break;
			case 24:
			case 25:
			case 30:
			case 50:
			case 60: hz = (fps << 8); break;
			default: trace (3, "ignoring non-standard frame rate %d fps\n", fps); break;
		}
	}

	if (!hz)
		return false;

	accumulate_fps (hz, SRC_VDEC);
	return true;
}

static void blackout ()
{
	mstime_disable (&g_ost_blackout);

	if (g_blackened)
		return;

	display_mode_get_current ();
	g_state.orig_mode = g_current_mode;
	display_mode_null ();

	update_stats ();
}

static void framerate_restore (bool only_if_black)
{
	mstime_disable (&g_ost_blackout);
	mstime_disable (&g_ost_switch);

	if (only_if_black && !g_blackened)
		return;

	if (g_state.orig_mode.name [0])
		display_mode_switch (&g_state.orig_mode, false);
	else
		display_mode_switch (&g_current_mode, false);

	memset (&g_state, 0, sizeof (g_state));
	update_stats ();
}

static void framerate_switch (bool force)
{
	if (g_state.restore) {
		if (!g_state.orig_mode.name [0])
			trace (1, "No saved display mode to restore\n");
		framerate_restore (false);
		return;
	}

	if (!g_enable) {
		trace (1, "User disabled AFR\n");
		framerate_restore (true);
		return;
	}

	if ((g_state.hz == 0) &&
	    mstime_expired (&g_state.hz_ost)) {
		g_state.hz = best_fps (true);
		if (g_state.hz == 0) {
			trace (1, "Timeout detecting movie frame rate, giving up\n");
			framerate_restore (true);
			return;
		}
	}

	// ask every source until we have a valid refresh rate
	if (g_state.hz == 0) {
		query_vdec_chunks ();
		query_vdec_blocks ();
		query_vdec ();
		g_state.hz = best_fps (false);
		if (g_state.hz == 0) {
			// Cannot determine movie frame rate, retry later
			if (g_switch_delay_retry)
				mstime_arm (&g_ost_switch, g_switch_delay_retry);
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

	trace (1, "Current mode is "DISPMODE_FMT"\n",
		DISPMODE_ARGS (g_current_mode, display_mode_hz (&g_current_mode)));
	trace (1, "Looking for display mode closest to %dx%d@"HZ_FMT"Hz\n",
		g_current_mode.width, g_current_mode.height, HZ_ARGS (g_state.hz));

	/* Find the video mode that:
	 * a) Has same width and height and interlace flag
	 * b) Closely divides by the required framerate, error less
	 *    than 4.1% (difference between 23.976 and 25 Hz)
	 * c) Has the highest framerate (e.g. 50Hz display modes are
	 *    better than 25Hz display modes for displaying 25Hz video)
	 *    if g_mode_prefer_exact is 0, or
	 * d) Has the closest framerate if g_mode_prefer_exact is 1.
	 */
	display_mode_t best_mode;
	unsigned best_rating = 0;
	int i;
	best_mode.name [0] = 0;
	for (i = 0; i < g_modes_n; i++) {
		display_mode_t *mode = &g_modes [i];
		if ((mode->width != g_current_mode.width) ||
		    (mode->height != g_current_mode.height) ||
		    (mode->interlaced != g_current_mode.interlaced))
			continue;

		unsigned rate_n = 1;
		unsigned rate = (mode->framerate << 16) / g_state.hz;
		while (rate > 0x180) {
			rate_n++;
			rate = (mode->framerate << 16) / (g_state.hz * rate_n);
		}

		unsigned delta = abs ((int)(rate - 0x100));
		if (delta > 11)
			continue; // freq error > 4.3%

		// rating is larger as delta is closer to 1.0 rate
		int rating = (11 - delta) * 16;

		unsigned n = (rate_n > 3) ? 3 : (rate_n - 1);
		rating += 4 * (g_mode_prefer_exact ? (3 - n) : n);

		if (rating > best_rating) {
			display_mode_t tmp = *mode;
			// decide if integer or fractional framerate is better
			display_mode_set_hz (&tmp, g_state.hz);

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

	if (!best_mode.name [0]) {
		trace (1, "Failed to find a suitable display mode\n");
		framerate_restore (true);
		return;
	}

	// if we already switched mode to something close to what we found,
	// avoid unneeded irritating framerate switching in the middle of watching
	if (g_state.orig_mode.name [0] && !g_blackened && !force) {
		int hz1 = display_mode_hz (&best_mode);
		int hz2 = display_mode_hz (&g_current_mode);
		if (hz_close (hz1, hz2)) {
			trace (1, "Skipping mode switch since current refresh is close enough\n");
			framerate_restore (true);
			return;
		}
	}

	mstime_disable (&g_ost_blackout);

	// remember current video mode to restore it later
	if (!g_state.orig_mode.name [0])
		g_state.orig_mode = g_current_mode;

	display_mode_switch (&best_mode, force);
	update_stats ();
}

/* @param restore true to delay restoring refresh rate to original,
 *      false to set refresh rate to match currently playing movie.
 * @param hz screen refresh rate in fixed-point 24.8 format if known,
 *      or 0 to ask movie refresh rate from vdec.
 * @param modalias the value of MODALIAS= uevent attribute (codec name)
 */
static void delay_framerate_switch (bool restore, int hz, const char *modalias)
{
	mstime_disable (&g_ost_blackout);
	mstime_disable (&g_ost_switch);

	if (g_switch_ignore) {
		if (restore)
			mstime_arm (&g_ost_off, g_switch_ignore);
		else if (mstime_enabled (&g_ost_off) &&
		         !mstime_expired (&g_ost_off) &&
		         !g_blackened) {
			trace (1, "Ignore framerate switch because restore event was %d ms ago\n",
				g_switch_ignore - mstime_left (&g_ost_off));
			g_state.restore = false;
			mstime_disable (&g_ost_blackout);
			mstime_disable (&g_ost_switch);
			update_stats ();
			return;
		}
	}

	mstime_t delay = restore ? g_switch_delay_off : g_switch_delay_on;

	if (restore && !g_switch_delay_off) {
		trace (1, "Refresh rate restoration disabled by user\n");

		framerate_restore (true);
		memset (&g_state, 0, sizeof (g_state));
		update_stats ();
		return;
	}

	if (modalias) {
		if (strlist_contains (&g_vdec_blacklist, modalias)) {
			trace (1, "Blacklisted vdec %s, skipping AFR\n", modalias);
			return;
		}

		// save decoder name
		strncpy (g_state.modalias, modalias, sizeof (g_state.modalias));
	}

	if (g_state.restore != restore) {
		g_state.restore = restore;
		g_state.hz = hz;
		// start collecting stats all over again
		memset (&g_state.hz_stat, 0, sizeof (g_state.hz_stat));
	}

	// if refresh rate is going to be restored, and screen is black, do not delay
	if (restore && g_blackened)
		delay = g_switch_delay_on;

	// check for frame_rate_hint via API
	if (!restore &&
	    mstime_enabled (&g_frame_rate_hint.stamp) &&
	    !mstime_expired (&g_frame_rate_hint.stamp))
		hz = g_frame_rate_hint.fps;

	// if we known fps, use it
	if (hz && (hz >= HZ_MIN) && (hz < HZ_MAX)) {
		accumulate_fps (hz, SRC_FRH);
		g_state.hz = best_fps (false);
	}

	if (g_state.hz)
		trace (1, "Delaying switch to "HZ_FMT"Hz by %d ms\n",
			HZ_ARGS (g_state.hz), delay);
	else if (restore)
		trace (1, "Delaying refresh rate restoration by %d ms\n", delay);
	else
		trace (1, "Starting framerate detection in %d ms\n", delay);

	mstime_arm (&g_ost_switch, delay);

	if (restore)
		mstime_disable (&g_state.hz_ost);
	else {
		mstime_arm (&g_state.hz_ost, g_switch_timeout);
		// when movie starts, disable screen until we switch to actual frame rate
		if (g_enable && g_switch_blackout &&
		    !g_state.hz && !g_state.orig_mode.name [0])
			mstime_arm (&g_ost_blackout, g_switch_blackout);
	}
}

static void handle_hdmi_switch ()
{
	int hs = sysfs_get_int (g_hdmi_state, NULL);
	if (hs <= 0) {
		trace (1, "HDMI not active, clearing video mode list\n");
		display_modes_fini ();
		memset (&g_state.orig_mode, 0, sizeof (g_state.orig_mode));
	} else {
		display_modes_init ();
		colorspace_refresh ();
	}
}

static void handle_uevent (char *msg, ssize_t size)
{
	const char *frame_rate_hint = NULL;
	const char *frame_rate_end_hint = NULL;
	const char *action = NULL;
	const char *modalias = NULL;
	const char *msg_orig = msg;

	uevent_filter_reset (&g_filter_frhint);
	uevent_filter_reset (&g_filter_vdec);
	uevent_filter_reset (&g_filter_hdmi);

	for (const char *end = msg + size; msg < end; msg += strlen (msg) + 1) {
		if (msg == msg_orig) {
			trace (2, "Parsing uevent %s\n", msg);
			continue;
		} else
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
		else if (strcmp (msg, "MODALIAS") == 0)
			modalias = val + strskip (val, "platform:");

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
				if (strlist_contains (&g_frhint_vdec_blacklist, g_state.modalias)) {
					trace (1, "Blacklisted vdec %s for FRAME_RATE_HINT, skipping\n",
					       g_state.modalias);
					return;
				}

				int fr_hz = (256*96000 + frh / 2) / frh;
				delay_framerate_switch (false, fr_hz, modalias);
			}
		} else if (frame_rate_end_hint)
			delay_framerate_switch (true, 0, modalias);

	} else if (uevent_filter_matched (&g_filter_vdec)) {
		/* got a vdec uevent */
		if (action && (strcmp (action, "add") == 0))
			delay_framerate_switch (false, 0, modalias);
		else if (action && (strcmp (action, "remove") == 0))
			delay_framerate_switch (true, 0, modalias);

	} else if (uevent_filter_matched (&g_filter_hdmi) && g_switch_hdmi) {
		/* hdmi plugged on or off */
		trace (1, "HDMI state changed, will handle in %d ms\n",
			g_switch_hdmi);
		mstime_arm (&g_ost_hdmi, g_switch_hdmi);

	} else
		trace (2, "\tUnrecognized uevent\n");
}

static void handle_uevents ()
{
	for (;;)
	{
		char msg [4096];
		struct sockaddr_nl addr;
		struct iovec iovec = {
			.iov_base = &msg,
			.iov_len = sizeof (msg) - 1,
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

		msg [size] = 0;
		handle_uevent (msg, size);
	}
}

/* --------- * --------- * --------- * --------- * --------- * --------- */

/* check config file once in 5 seconds */
#define CONFIG_CHECK_PERIOD	5000

static int min_time (int to, mstime_t *ost)
{
	int tost = mstime_left (ost);
	if ((to < 0) || ((tost >= 0) && (tost < to)))
		return tost;
	return to;
}

static time_t mtime (const char *fn)
{
	struct stat st;
	if (stat (fn, &st) == 0)
		return st.st_mtime;
	return 0;
}

static void mstime_adjust (int delta, mstime_t *ost)
{
	if (mstime_enabled (ost))
		*ost += delta;
}

static void safe_mstime_update (int to)
{
	int old_mstime = g_mstime;
	mstime_update ();

	// time difference is substantially larger than timeout?
	int delta_mstime = g_mstime - old_mstime;
	if ((delta_mstime < 0) || (delta_mstime > to + 10000)) {
		delta_mstime -= to;
		trace (1, "System timer changed, adjusting all timers by %d ms\n", delta_mstime);
		mstime_adjust (delta_mstime, &g_ost_switch);
		mstime_adjust (delta_mstime, &g_ost_hdmi);
		mstime_adjust (delta_mstime, &g_ost_blackout);
		mstime_adjust (delta_mstime, &g_ost_config);
	}
}

static time_t g_config_mtime;

int afrd_run ()
{
	if (g_uevent_sock == -1)
		return -1;

	int ret = 0;

	trace (1, "afrd running\n");
	mstime_update ();

	struct pollfd pfd [16];
	pfd [0].events = POLLIN;
	pfd [0].fd = g_uevent_sock;

	mstime_disable (&g_ost_switch);
	mstime_disable (&g_ost_hdmi);
	mstime_disable (&g_ost_blackout);
	mstime_disable (&g_ost_off);

	// Check config timestamp timer
	mstime_arm (&g_ost_config, 1);
	g_config_mtime = mtime (g_config);

	update_stats ();

	while (!g_shutdown) {
		// flush log to disk
		trace_sync ();

		// update the millisecond timer
		safe_mstime_update (0);

		int to = mstime_left (&g_ost_switch);
		to = min_time (to, &g_ost_hdmi);
		to = min_time (to, &g_ost_blackout);
		to = min_time (to, &g_ost_config);

		// this should never happen as g_ost_config is always active, but
		// anyway don't allow to sleep indefinitely 'cause we can't detect
		// system time changes
		if (to < 0)
			to = 60000;

		// wait until either a new uevent comes
		// or the delayed mode switch timer expires
		pfd [0].revents = 0;
		// add API sockets into the pool
		int n_pfd = 1 + apisock_prep_poll (pfd + 1, ARRAY_SIZE (pfd) - 1);
		int rc = poll (pfd, n_pfd, to);

		// catch system time change events, this breaks our timers
		safe_mstime_update (to);

		if (rc > 0) {
			if (pfd [0].revents & POLLIN)
				handle_uevents ();
			apisock_handle (pfd, n_pfd);
		}

		// disable screen at start of playback
		if (mstime_expired (&g_ost_blackout) && !g_state.restore)
			blackout ();

		// if mode switch timer expired, switch the mode finally
		if (mstime_expired (&g_ost_switch)) {
			mstime_disable (&g_ost_switch);
			framerate_switch (false);
		}

		// query supported video modes after HDMI has been plugged on
		if (mstime_expired (&g_ost_hdmi)) {
			mstime_disable (&g_ost_hdmi);
			handle_hdmi_switch ();
		}

		// check config timestamp and reload it if so
		if (mstime_expired (&g_ost_config)) {
			// if we're doing other work, don't hog the CPU
			if (!mstime_enabled (&g_ost_blackout) &&
			    !mstime_enabled (&g_ost_switch) &&
			    !mstime_enabled (&g_ost_hdmi)) {
				mstime_arm (&g_ost_config, CONFIG_CHECK_PERIOD);
				time_t cmt = mtime (g_config);
				if ((cmt != 0) && (cmt != g_config_mtime)) {
					trace (1, "config file %s changed, reloading\n", g_config);
					ret = 1;
					break;
				}
			} else
				mstime_arm (&g_ost_config, 1000);
		}
	}

	// restore framerate just in case
	g_state.restore = true;
	framerate_switch (false);
	return ret;
}

/* --------- * --------- * --------- * --------- * --------- * --------- */

void afrd_frame_rate_hint (int hz)
{
	g_frame_rate_hint.fps = hz;
	mstime_arm (&g_frame_rate_hint.stamp, 1000);
}

void afrd_refresh_rate (int hz)
{
	bool ok = (hz && (hz >= HZ_MIN) && (hz < HZ_MAX));
	if (!ok)
		hz = 0;

	g_state.restore = !ok;
	g_state.hz = hz;
	framerate_switch (true);

}

void afrd_reconf ()
{
	mstime_arm (&g_ost_config, 0);
	g_config_mtime = (time_t)-1;
}

/* --------- * --------- * --------- * --------- * --------- * --------- */

static void blacklist_rates_load (const char *kw)
{
	g_mode_blacklist_rates_count = 0;

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
		    (g_mode_blacklist_rates_count < ARRAY_SIZE (g_mode_blacklist_rates))) {
			int irate = (int)(256.0 * rate + 0.5);
			g_mode_blacklist_rates [g_mode_blacklist_rates_count] = irate;
			g_mode_blacklist_rates_count++;
			trace (2, "\t+ "HZ_FMT"Hz\n", HZ_ARGS (irate));
		}

		cur = next;
	}

	free (tmp);
}

int afrd_init ()
{
	/* load config if not loaded already */
	if (!g_cfg && (load_config (g_config) != 0))
		return -1;

	shmem_init (false);

	int log_enable = (cfg_get_int ("log.enable", 1) != 0);
	const char *log_file = cfg_get_str ("log.file", NULL);
	if (log_file && log_enable)
		trace_log (log_file);

	trace (1, "afrd v%s built at %s is initializing\n", g_version, g_bdate);
	trace (1, "\tactive config file: %s\n", g_config);

	g_enable = (cfg_get_int ("enable", 1) != 0);

	g_hdmi_dev = cfg_get_str ("hdmi.sysfs", DEFAULT_HDMI_DEV);
	g_hdmi_state = cfg_get_str ("hdmi.state", DEFAULT_HDMI_STATE);

	g_mode_path = cfg_get_str ("mode.path", DEFAULT_VIDEO_MODE);
	g_mode_prefer_exact = cfg_get_int ("mode.prefer.exact", DEFAULT_MODE_PREFER_EXACT);
	g_mode_use_fract = cfg_get_int ("mode.use.fract", DEFAULT_MODE_USE_FRACT);
	blacklist_rates_load ("mode.blacklist.rates");

	trace (1, "\trefresh rate selection: use fractional %d, exact %d\n",
		g_mode_use_fract, g_mode_prefer_exact);

	g_switch_delay_on = cfg_get_int ("switch.delay.on", DEFAULT_SWITCH_DELAY_ON);
	g_switch_delay_off = cfg_get_int ("switch.delay.off", DEFAULT_SWITCH_DELAY_OFF);
	g_switch_delay_retry = cfg_get_int ("switch.delay.retry", DEFAULT_SWITCH_DELAY_RETRY);
	g_switch_timeout = cfg_get_int ("switch.timeout", DEFAULT_SWITCH_TIMEOUT);
	g_switch_blackout = cfg_get_int ("switch.blackout", DEFAULT_SWITCH_BLACKOUT);
	g_switch_ignore = cfg_get_int ("switch.ignore", DEFAULT_SWITCH_IGNORE);
	g_switch_hdmi = cfg_get_int ("switch.hdmi", DEFAULT_SWITCH_HDMI);

	trace (1, "\tswitch delays: on %d, off %d, retry %d ms\n",
		g_switch_delay_on, g_switch_delay_off, g_switch_delay_retry);
	trace (1, "\t\ttimeout %d ms, blackout %d ms, ignore %d ms\n",
		g_switch_timeout, g_switch_blackout, g_switch_ignore);

	g_vdec_sysfs = cfg_get_str ("vdec.sysfs", DEFAULT_VDEC_SYSFS);
	strlist_load (&g_vdec_blacklist, "vdec.blacklist", "vdec blacklist");
	strlist_load (&g_frhint_vdec_blacklist, "frhint.vdec.blacklist", "frhint vdec blacklist");
	uevent_filter_load (&g_filter_frhint, "uevent.filter.frhint");
	uevent_filter_load (&g_filter_vdec, "uevent.filter.vdec");
	uevent_filter_load (&g_filter_hdmi, "uevent.filter.hdmi");

	if (!uevent_open (16 * 1024)) {
		trace (0, "failed to open uevent socket");
		return EPERM;
	}

	display_modes_init ();
	colorspace_init ();
	apisock_init ();

	return 0;
}

void afrd_fini ()
{
	if (g_uevent_sock != -1) {
		close (g_uevent_sock);
		g_uevent_sock = -1;
	}

	uevent_filter_fini (&g_filter_frhint);
	uevent_filter_fini (&g_filter_vdec);
	uevent_filter_fini (&g_filter_hdmi);
	strlist_free (&g_vdec_blacklist);
	g_mode_blacklist_rates_count = 0;

	g_enable = false;
	g_hdmi_dev = NULL;
	g_hdmi_state = NULL;
	g_mode_path = NULL;
	g_vdec_sysfs = NULL;

	apisock_fini ();
	display_modes_fini ();
	colorspace_fini ();

	if (g_cfg) {
		cfg_free (g_cfg);
		g_cfg = NULL;
	}

	shmem_fini ();
	trace_log (NULL);
}
