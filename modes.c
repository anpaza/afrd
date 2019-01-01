/*
 * Automatic Framerate Daemon
 * for AMLogic S905/S912-based boxes.
 */

#include "afrd.h"

display_mode_t *g_modes = NULL;
int g_modes_n = 0;
display_mode_t g_current_mode;

static int parse_int (char **line)
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

static bool mode_parse (char *desc, display_mode_t *mode)
{
	memset (mode, 0, sizeof (display_mode_t));
	mode->name = desc;

	if (strncmp (desc, "smpte", 5) == 0) {
		mode->width = 3840;
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
				  default: mode->name = NULL; return false;
			}
		}
	}

	char c = *desc++;
	if (c == 'i')
		mode->interlaced = true;
	else if (c == 'p')
		mode->interlaced = false;
	else {
		mode->name = NULL;
		return false;
	}

	mode->framerate = parse_int (&desc);
	return true;
}

int display_modes_init ()
{
	display_modes_finit ();

	char *modes = sysfs_get_str (g_hdmi_dev, "disp_cap");

	trace (2, "Parsing supported video modes\n");

	// parse the list of video modes supported by display
	while (modes && *modes) {
		modes += strspn (modes, " \t\r\n");
		int mode_len = strcspn (modes, "\r\n");
		if (!mode_len)
			break;

		if (modes [mode_len - 1] == '*')
			mode_len--;
		char *mode_name = strndup (modes, mode_len);

		display_mode_t mode;
		if (mode_parse (mode_name, &mode)) {
			g_modes_n++;
			g_modes = (display_mode_t *)realloc (g_modes, sizeof (display_mode_t) * g_modes_n);
			g_modes [g_modes_n - 1] = mode;

			trace (2, "\t"DISPMODE_FMT"\n",
				DISPMODE_ARGS (mode, display_mode_hz (&mode)));
		} else {
			trace (2, "\t%s: unrecognized mode\n", mode_name);
			free (mode_name);
		}

		modes = strchr (modes + mode_len, '\n');
	}

	free (modes);

	// now parse the current video mode
	char *mode = sysfs_get_str (g_video_mode, NULL);
	if (!mode_parse (mode, &g_current_mode)) {
		trace (1, "Failed to recognize current video mode '%s'\n", mode);
		free (mode);
		return -1;
	}

	g_current_mode.fractional = false;
	char *frac_rate = sysfs_get_str (g_hdmi_dev, "frac_rate_policy");
	if (!frac_rate)
		fprintf (stderr, "%s: failed to read frac_rate_policy!\n", g_program);
	else
	{
		g_current_mode.fractional = strtol (frac_rate, NULL, 10) != 0;
		free (frac_rate);
	}

	return 0;
}

void display_modes_finit ()
{
	int i;

	for (i = 0; i < g_modes_n; i++)
		free (g_modes [i].name);
	free (g_modes);

	g_modes = NULL;
	g_modes_n = 0;
}

bool display_mode_equal (display_mode_t *mode1, display_mode_t *mode2)
{
	return (mode1->width == mode2->width) &&
	       (mode1->height == mode2->height) &&
	       (mode1->framerate == mode2->framerate) &&
	       (mode1->interlaced == mode2->interlaced) &&
	       (mode1->fractional == mode2->fractional);
}

unsigned display_mode_hz (display_mode_t *mode)
{
	unsigned hz = mode->framerate << 8;

	if (mode->fractional)
		switch (hz) {
			case ( 24 << 8): hz = ( 2997 * 256 + 62) / 125; break;
			case ( 30 << 8): hz = ( 2997 * 256 + 50) / 100; break;
			case ( 60 << 8): hz = ( 5994 * 256 + 50) / 100; break;
			case (120 << 8): hz = (11988 * 256 + 50) / 100; break;
			case (240 << 8): hz = (23976 * 256 + 50) / 100; break;
		}

	return hz;
}

void display_mode_set_hz (display_mode_t *mode, unsigned hz)
{
	mode->fractional = true;
	unsigned hz_frac = display_mode_hz (mode);
	unsigned hz_int = mode->framerate << 8;

	// find multiple of hz closest to hz_int
	unsigned hz_n = 1;
	unsigned best_hz = hz;
	unsigned best_diff = abs ((int)hz - (int)hz_int);
	for (;;) {
		hz_n++;
		unsigned multiple_hz = hz * hz_n;
		unsigned multiple_diff = abs ((int)multiple_hz - (int)hz_int);
		if (multiple_diff > best_diff)
			break;
		best_hz = multiple_hz;
		best_diff = multiple_diff;
	}
	hz = best_hz;

	if (hz_frac == hz_int) {
		mode->fractional = false;
		return;
	}

	if (abs ((int)hz_int - (int)hz) < abs ((int)hz_frac - (int)hz))
		mode->fractional = false;
}

void display_mode_switch (display_mode_t *mode)
{
	trace (1, "Switching display mode to "DISPMODE_FMT"\n",
		DISPMODE_ARGS (*mode, display_mode_hz (mode)));

	if (mode->fractional != g_current_mode.fractional) {
		// fractional mode transition via special null mode
		char frac [8];
		snprintf (frac, sizeof (frac), "%d", mode->fractional ? 1 : 0);
		sysfs_write (g_video_mode, "null");
		sysfs_set_str (g_hdmi_dev, "frac_rate_policy", frac);
	}

	sysfs_write (g_video_mode, mode->name);
	g_current_mode = *mode;
}
