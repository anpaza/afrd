/*
 * Automatic Framerate Daemon
 * for AMLogic S905/S912-based boxes.
 */

#include "afrd.h"

display_mode_t *g_modes = NULL;
int g_modes_n = 0;
display_mode_t g_current_mode;

static int parse_int (const char **line)
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

static bool mode_parse (const char *desc, display_mode_t *mode)
{
	memset (mode, 0, sizeof (display_mode_t));

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
				  default: return false;
			}
		}
	}

	char c = *desc++;
	if (c == 'i')
		mode->interlaced = true;
	else if (c == 'p')
		mode->interlaced = false;
	else
		return false;

	mode->framerate = parse_int (&desc);
	return true;
}

int display_mode_init ()
{
	char *modes = sysfs_get_str (g_hdmi_dev, "disp_cap");
	free (g_modes);
	g_modes = NULL;

	// parse the list of video modes supported by display
	while (modes && *modes) {
		modes += strspn (modes, " \t\r\n");
		int mode_len = strcspn (modes, "\r\n");
		if (modes [mode_len - 1] == '*')
			mode_len--;
		char *mode_name = strndup (modes, mode_len);

		display_mode_t mode;
		if (mode_parse (mode_name, &mode)) {
			g_modes_n++;
			g_modes = (display_mode_t *)realloc (g_modes, sizeof (display_mode_t) * g_modes_n);
			mode.name = mode_name;
			memcpy (g_modes + g_modes_n - 1, &mode, sizeof (mode));

			trace (2, "adding supported mode %s: %dx%d@%dhz%s\n",
				mode.name, mode.width, mode.height, mode.framerate, mode.interlaced ? ", interlaced" : "");
		} else
			free (mode_name);

		modes = strchr (modes + mode_len, '\n');
	}

	free (modes);

	// now parse the current video mode
	char *mode = sysfs_read (g_video_mode);
	if (!mode_parse (mode, &g_current_mode)) {
		trace (1, "Failed to recognize current video mode\n");
		free (mode);
		return -1;
	}
	free (mode);

	return 0;
}

bool display_mode_equal (display_mode_t *mode1, display_mode_t *mode2)
{
	return ((mode1->width == mode2->width) &&
		(mode1->height == mode2->height) &&
		(mode1->framerate == mode2->framerate) &&
		(mode1->interlaced == mode2->interlaced)) ? true : false;
}
