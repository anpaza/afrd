#ifndef __UEVENT_FILTER_H__
#define __UEVENT_FILTER_H__

#include <regex.h>

typedef struct
{
	// Attribute names
	const char *attr [16];
	// Regular expressions for attribute values
	regex_t rex [16];
	// Filter name
	char *name;
	// Original filter string, modified for our needs
	char *filter;
	// Number of attributes
	int size;
	// Number of matches since last reset
	int matches;
} uevent_filter_t;

/// Initialize an uEvent filter object from filter expression string
extern bool uevent_filter_init (uevent_filter_t *uevf, const char *name, const char *filter);
/// Finalize an uEvent filter
extern void uevent_filter_fini (uevent_filter_t *uevf);
/// Load filter expression from config file
extern bool uevent_filter_load (uevent_filter_t *uevf, const char *kw);
/// Reset uEvent filter before doing any matches
extern void uevent_filter_reset (uevent_filter_t *uevf);
/// Match attribute against the filter
extern bool uevent_filter_match (uevent_filter_t *uevf, const char *attr, const char *value);
/// Check if all attributes were matched
extern bool uevent_filter_matched (uevent_filter_t *uevf);

/// Helper function
void strip_trailing_spaces (char *eol, const char *start);

#endif /* __UEVENT_FILTER_H__ */
