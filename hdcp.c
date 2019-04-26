/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Video mode handling & switching
 */

#include "afrd.h"

// 0 - not supported, 1 - HDCP 1.4, 2 - HDCP 2.2
int g_hdcp_enabled = 0;

void hdcp_init ()
{
	char *hdcp = sysfs_get_str (g_hdmi_dev, "hdcp_mode");
	char *cur = hdcp + strspn (hdcp, spaces);
	strip_trailing_spaces (strchr (cur, 0), cur);
	g_hdcp_enabled = 0;
	if (!strcmp (cur, "off")) {
		g_hdcp_enabled = 0;
		trace (1, "HDCP is not enabled\n");
	}
	else if (!strcmp (cur, "14")) {
		g_hdcp_enabled = 1;
		trace (1, "HDCP 1.4 is enabled\n");
	}
	else if (!strcmp (cur, "22")) {
		g_hdcp_enabled = 2;
		trace (1, "HDCP 2.2 is enabled\n");
	}
	else
		trace (1, "Unrecognized HDCP mode: %s\n", cur);
	free (hdcp);
}

void hdcp_fini ()
{
	g_hdcp_enabled = 0;
}

void hdcp_restore (bool force)
{
	static const char hdcp_mode [][3] =
	{
		"0", "14", "22"
	};

	if (force && (g_hdcp_enabled == 0))
		g_hdcp_enabled = 1;

	if ((g_hdcp_enabled == 0) || g_blackened)
		return;

	const char *mode = hdcp_mode [g_hdcp_enabled];
	sysfs_set_str (g_hdmi_dev, "hdcp_mode", mode);
	trace (1, "Setting HDCP mode to %s\n", mode);
}

void hdcp_check ()
{
	if ((g_hdcp_enabled == 0) || g_blackened)
		return;

	char *auth = sysfs_read (DEFAULT_HDCP_AUTHENTICATED);
	char *cur = auth + strspn (auth, spaces);
	strip_trailing_spaces (strchr (cur, 0), cur);
	bool disabled = (strcmp (cur, "0") == 0);
	free (auth);

	if (disabled)
		hdcp_restore (false);
}
