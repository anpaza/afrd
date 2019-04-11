/* Support for ANDROid Properties */
#ifndef __ANDROP_H__
#define __ANDROP_H__

#ifdef ANDROID

// Initialize the storage for property values
extern void androp_init ();

// Free static storage allocated for property values
extern void androp_fini ();

// Get a pointer to a statically allocated buffer with property value
extern const char *androp_get (const char *key);

#else

#define androp_init()
#define androp_fini()
#define androp_get(key) NULL

#endif

#endif /* __ANDROP_H__ */
