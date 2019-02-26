#ifndef __COLORSPACE_H__
#define __COLORSPACE_H__

#include <stdbool.h>

/// load colorspace-related stuff from config file
extern void colorspace_init ();
/// free all memory occupied by colorspace stuff
extern void colorspace_fini ();
/// refresh current list of supported color spaces
extern bool colorspace_refresh ();
/// select and apply color space dependent on video mode
extern bool colorspace_apply (const char *mode);

#endif /* __COLORSPACE_H__ */
