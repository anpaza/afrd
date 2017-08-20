/*
 * Main header file
 */

#ifndef __AFRD_H__
#define __AFRD_H__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "mstime.h"
#include "cfg_parse.h"

#define DEFAULT_HDMI_DEV		"/sys/class/amhdmitx/amhdmitx0"
#define DEFAULT_HDMI_STATE		"/sys/class/switch/hdmi/state"
#define DEFAULT_VIDEO_MODE		"/sys/class/display/mode"
#define DEFAULT_MODE_SWITCH_DELAY_ON	100
#define DEFAULT_MODE_SWITCH_DELAY_OFF	2000

#define ARRAY_SIZE(x)			(sizeof (x) / sizeof (x [0]))

typedef struct
{
	char *name;
	int width;
	int height;
	int framerate;
	bool interlaced;
} display_mode_t;

// the global config
extern struct cfg_struct *g_cfg;
// trace calls if non-zero
extern int g_verbose;
// set asynchronously to 1 to initiate shutdown
extern volatile int g_shutdown;
// sysfs path to hdmi interface
extern const char *g_hdmi_dev;
// sysfs path to current display mode
extern const char *g_video_mode;

// the list of supported video modes
extern display_mode_t *g_modes;
// number of video modes in the list
extern int g_modes_n;
// current video mode
extern display_mode_t g_current_mode;
// the delay before switching display mode
extern int g_mode_switch_delay;

// trace calls if g_verbose != 0
extern void trace (int level, const char *format, ...);

extern int afrd_init ();
extern void afrd_run ();
extern void afrd_fini ();

// read the list of all supported display modes and current display mode
extern int display_mode_init ();
// check if two display modes have same attributes
extern bool display_mode_equal (display_mode_t *mode1, display_mode_t *mode2);

// superstructure on cfg_parse
extern const char *cfg_get_str (const char *key, const char *defval);
extern int cfg_get_int (const char *key, int defval);

// helper functions for sysfs
extern char *sysfs_read (const char *device_attr);
extern char *sysfs_get_str (const char *device, const char *attr);
extern int sysfs_get_int (const char *device, const char *attr);

extern int sysfs_write (const char *device_attr, const char *value);
extern int sysfs_set_str (const char *device, const char *attr, const char *value);
extern int sysfs_set_int (const char *device, const char *attr, int value);

extern int sysfs_exists (const char *device_attr);

#endif /* __AFRD_H__ */
