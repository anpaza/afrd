/*
 * Main header file
 */

#ifndef __AFRD_H__
#define __AFRD_H__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include "mstime.h"
#include "cfg_parse.h"

#define DEFAULT_HDMI_DEV		"/sys/class/amhdmitx/amhdmitx0"
#define DEFAULT_HDMI_STATE		"/sys/class/switch/hdmi/state"
#define DEFAULT_HDMI_DELAY		300
#define DEFAULT_VIDEO_MODE		"/sys/class/display/mode"
#define DEFAULT_VDEC_SYSFS		"/sys/class/vdec"
#define DEFAULT_SWITCH_DELAY_ON		250
#define DEFAULT_SWITCH_DELAY_OFF	5000
#define DEFAULT_SWITCH_DELAY_RETRY	500
#define DEFAULT_SWITCH_TIMEOUT		3000
#define DEFAULT_SWITCH_BLACKOUT		50
#define DEFAULT_MODE_PREFER_EXACT	0
#define DEFAULT_MODE_USE_FRACT		0

#define ARRAY_SIZE(x)			(sizeof (x) / sizeof (x [0]))

typedef struct
{
	char name [32];
	int width;
	int height;
	int framerate;
	bool interlaced;
	bool fractional;
} display_mode_t;

// printf ("mode: "DISPMODE_FMT, DISPMODE_ARGS(mode, display_mode_hz (&mode)))
#define DISPMODE_FMT			"%s (%ux%u@%u.%02uHz%s)"
#define DISPMODE_ARGS(mode, hz) \
	(mode).name, (mode).width, (mode).height, \
	(hz) >> 8, (100 * ((hz) & 255)) >> 8, \
	(mode).interlaced ? ", interlaced" : ""

// program name
extern const char *g_program;
// program version
extern const char *g_version;
// program build date/time
extern const char *g_bdate;
// the file name of the active config
extern const char *g_config;
// the global config
extern struct cfg_struct *g_cfg;
// trace calls if non-zero
extern int g_verbose;
// set asynchronously to 1 to initiate shutdown
extern volatile int g_shutdown;
// sysfs path to hdmi interface
extern const char *g_hdmi_dev;
// sysfs path to current display mode
extern const char *g_mode_path;

// the list of supported video modes
extern display_mode_t *g_modes;
// number of video modes in the list
extern int g_modes_n;
// current video mode
extern display_mode_t g_current_mode;
// true if screen is disabled
extern bool g_current_null;
// the delay before switching display mode
extern int g_mode_switch_delay;

// trace calls if g_verbose != 0
extern void trace (int level, const char *format, ...);
// enable logging trace()s to file
extern void trace_log (const char *logfn);

extern int afrd_init ();
extern int afrd_run ();
extern void afrd_fini ();

// read the list of all supported display modes and current display mode
extern int display_modes_init ();
// free the list of supported modes
extern void display_modes_fini ();
// query the current video mode
extern void display_mode_get_current ();
// check if two display modes have same attributes
extern bool display_mode_equal (display_mode_t *mode1, display_mode_t *mode2);
// return display mode refresh rate in 24.8 fixed-point format
extern int display_mode_hz (display_mode_t *mode);
// set fractional framerate if that is closer to hz (24.8 fixed-point)
extern void display_mode_set_hz (display_mode_t *mode, int hz);
// switch video mode
extern void display_mode_switch (display_mode_t *mode);
// disable the screen
extern void display_mode_null ();

// load config from file
extern int load_config (const char *config);
// superstructure on cfg_parse
extern const char *cfg_get_str (const char *key, const char *defval);
extern int cfg_get_int (const char *key, int defval);

// helper functions for sysfs
extern char *sysfs_read (const char *device_attr);
// unlike _read, removes trailing spaces and newlines
extern char *sysfs_get_str (const char *device, const char *attr);
extern int sysfs_get_int (const char *device, const char *attr);

extern int sysfs_write (const char *device_attr, const char *value);
extern int sysfs_set_str (const char *device, const char *attr, const char *value);
extern int sysfs_set_int (const char *device, const char *attr, int value);

extern int sysfs_exists (const char *device_attr);

// " \t\r\n"
extern const char *spaces;

// Return strlen(starts) if str starts with it, 0 otherwise
extern int strskip (const char *str, const char *starts);
// Move backwards from eol down to start, replacing the last space with a \0
extern void strip_trailing_spaces (char *eol, const char *start);
// Evaluates the number pointed by line until a non-digit is encountered
extern int parse_int (char **line);
// Similar but first looks for number prefix in line and sets ok to false on error
extern unsigned long find_ulong (const char *str, const char *prefix, bool *ok);
// Same but returns a unsigned long long
extern unsigned long long find_ulonglong (const char *str, const char *prefix, bool *ok);

typedef struct
{
	// number of elements in the list
	int size;
	// array of string pointers
	char **data;
} strlist_t;

// Load a space-separated list from config key
extern bool strlist_load (strlist_t *list, const char *key, const char *desc);
// Free a string list
extern void strlist_free (strlist_t *list);
// Check if string list contains selected value
extern bool strlist_contains (strlist_t *list, const char *str);

#endif /* __AFRD_H__ */
