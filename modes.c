/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Video mode handling & switching
 */

#include "afrd.h"
#include "colorspace.h"

display_mode_t *g_modes = NULL;
int g_modes_n = 0;
display_mode_t g_current_mode;
bool g_blackened = false;

static bool mode_parse (char *desc, display_mode_t *mode)
{
	memset (mode, 0, sizeof (display_mode_t));
	if (!desc)
		return false;

	strncpy (mode->name, desc, sizeof (mode->name));

	if (strncmp (desc, "smpte", 5) == 0) {
		mode->width = 4096;
		mode->height = 2160;
		desc += 5;
	} else {
		int v = parse_int (&desc);
		char c = *desc;
		if (c == 'x') {
			mode->width = v;
			mode->height = parse_int (&desc);
		} else {
			switch ((mode->height = v))
			{
				case  480: mode->width =  640; break;
				case  576: mode->width =  720; break;
				case  720: mode->width = 1280; break;
				case 1080: mode->width = 1920; break;
				case 2160: mode->width = 3840; break;
				  default: mode->name [0] = 0; return false;
			}
		}

		c = *desc++;
		// according to kernel sources, 'fp' means same as 'p'
		if (c == 'f')
			c = *desc++;
		if (c == 'i')
			mode->interlaced = true;
		else if (c == 'p')
			mode->interlaced = false;
		else {
			mode->name [0] = 0;
			return false;
		}
	}

	mode->framerate = parse_int (&desc);

	// here follows 'hz' optionally followed by color space like '420'.
	// we ignore them.

	return true;
}

void display_mode_add (display_mode_t *mode)
{
	/* keep only non-fractional modes in list */
	display_mode_t new_mode = *mode;
	new_mode.fractional = false;

	/* check if mode is already in list */
	for (int i = 0; i < g_modes_n; i++)
		if (display_mode_equal (&new_mode, &g_modes [i]))
			return;

	g_modes_n++;
	g_modes = (display_mode_t *)realloc (g_modes, sizeof (display_mode_t) * g_modes_n);
	g_modes [g_modes_n - 1] = new_mode;

	trace (2, "\t"DISPMODE_FMT"\n", DISPMODE_ARGS (new_mode, display_mode_hz (&new_mode)));
}

int display_modes_init ()
{
	display_modes_fini ();

	char *modes = sysfs_get_str (g_hdmi_dev, "disp_cap");
	if (!modes)
		return -1;

	trace (2, "Parsing supported video modes\n");

	// parse the list of video modes supported by display
	char *cur = modes;
	while (cur && *cur) {
		cur += strspn (cur, spaces);
		int mode_len = strcspn (cur, spaces);
		if (!mode_len)
			break;

		if (cur [mode_len - 1] == '*')
			mode_len--;
		cur [mode_len] = 0;

		display_mode_t mode;
		if (mode_parse (cur, &mode))
			display_mode_add (&mode);
		else
			trace (2, "\t%s: unrecognized mode\n", cur);

		cur += mode_len + 1;
	}

	free (modes);

	display_mode_get_current ();

	// on some weird configs current video mode may not be listed in disp_cap
	if (g_current_mode.name [0])
		display_mode_add (&g_current_mode);

	// add extra user-specified modes from config
	strlist_t xmodes;
	if (strlist_load (&xmodes, "mode.extra", "extra video modes")) {
		for (int i = 0; i < xmodes.size; i++) {
			display_mode_t mode;
			if (mode_parse (xmodes.data [i], &mode))
				display_mode_add (&mode);
		}
		strlist_free (&xmodes);
	}

	return 0;
}

void display_mode_get_current ()
{
	// parse the current video mode
	char *mode = sysfs_get_str (g_mode_path, NULL);
	if (!strcmp (mode, "null")) {
		trace (1, "Current video mode is null!\n");
		return;
	}

	if (!mode_parse (mode, &g_current_mode)) {
		trace (1, "Failed to recognize current video mode '%s'\n", mode);
		free (mode);
		return;
	}

	g_current_mode.fractional = false;
	char *frac_rate = sysfs_get_str (g_hdmi_dev, "frac_rate_policy");
	if (!frac_rate)
		trace (1, "failed to read frac_rate_policy!\n");
	else
	{
		g_current_mode.fractional = strtol (frac_rate, NULL, 10) != 0;
		free (frac_rate);
	}
}

void display_modes_fini ()
{
	free (g_modes);

	g_modes = NULL;
	g_modes_n = 0;
}

bool display_mode_equal (display_mode_t *mode1, display_mode_t *mode2)
{
	if ((mode1->width != mode2->width) ||
	    (mode1->height != mode2->height) ||
	    (mode1->interlaced != mode2->interlaced))
		return false;

	int hz1 = display_mode_hz (mode1);
	int hz2 = display_mode_hz (mode2);
	return (hz1 == hz2);
}

int display_mode_hz (display_mode_t *mode)
{
	if (mode->fractional)
		switch (mode->framerate) {
			case  24: return ( 2997 * 256 + 62) / 125;
			case  30: return ( 2997 * 256 + 50) / 100;
			case  60: return ( 5994 * 256 + 50) / 100;
			case 120: return (11988 * 256 + 50) / 100;
			case 240: return (23976 * 256 + 50) / 100;
		}

	return mode->framerate * 256;
}

void display_mode_set_hz (display_mode_t *mode, int hz)
{
	mode->fractional = true;
	int hz_frac = display_mode_hz (mode);
	int hz_int = mode->framerate * 256;

	if (hz_frac == hz_int) {
		// this mode has no fractional variant
		mode->fractional = false;
		return;
	}

	// find multiple of hz closest to hz_int
	int hz_n = 1;
	int best_hz = hz;
	int best_diff = abs (hz - hz_int);
	for (;;) {
		hz_n++;
		int multiple_hz = hz * hz_n;
		int multiple_diff = abs (multiple_hz - hz_int);
		if (multiple_diff > best_diff)
			break;
		best_hz = multiple_hz;
		best_diff = multiple_diff;
	}

	if (abs (hz_int - best_hz) < abs (hz_frac - best_hz))
		mode->fractional = false;
}

void display_mode_switch (display_mode_t *mode, bool force)
{
	if (!g_blackened && !force &&
	    display_mode_equal (mode, &g_current_mode)) {
		trace (1, "Display mode is already "DISPMODE_FMT"\n",
			DISPMODE_ARGS (*mode, display_mode_hz (mode)));
		return;
	}

	// fractional mode transition via special null mode
	if (force ||
	    ((strcmp (mode->name, g_current_mode.name) == 0) &&
	     (mode->fractional != g_current_mode.fractional)))
		display_mode_null ();

	trace (1, "Switching display mode to "DISPMODE_FMT"\n",
		DISPMODE_ARGS (*mode, display_mode_hz (mode)));

	char frac [2] = { mode->fractional ? '1' : '0', 0 };
	sysfs_set_str (g_hdmi_dev, "frac_rate_policy", frac);

	colorspace_apply (mode->name);

	sysfs_write (g_mode_path, mode->name);
	g_current_mode = *mode;
	g_blackened = false;
}

void display_mode_null ()
{
	if (g_blackened)
		return;

	trace (2, "Blackout screen\n");
	sysfs_write (g_mode_path, "null");
	g_blackened = true;
}
