/* Support for Android settings via external tool */

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#ifdef ANDROID

// Initialize the storage for property values
extern void settings_init ();

// Free static storage allocated for property values
extern void settings_fini ();

// Get a pointer to a statically allocated buffer with property value
extern const char *settings_get (const char *key);

// Wrapper around settings_get that prepends "afrd_" to key
extern const char *settings_afrd_get (const char *key);

// Get a integer setting
extern int settings_get_int (const char *key, int defval);

#else

#define settings_init()
#define settings_fini()
#define settings_get(key) NULL
#define settings_afrd_get(key) NULL
#define settings_get_int(key,def) def

#endif

#endif /* __SETTINGS_H__ */
