#include "afrd.h"
#include "colorspace.h"
#include <regex.h>
#include <string.h>

/* blatantly borrowed from include/linux/amlogic/media/vout/hdmi_tx/hdmi_common.h */
enum hdmi_color_space {
	COLORSPACE_RGB444 = 0,
	COLORSPACE_YUV422 = 1,
	COLORSPACE_YUV444 = 2,
	COLORSPACE_YUV420 = 3,
	COLORSPACE_RESERVED,
};

enum hdmi_color_depth {
	COLORDEPTH_24B = 4,
	COLORDEPTH_30B = 5,
	COLORDEPTH_36B = 6,
	COLORDEPTH_48B = 7,
	COLORDEPTH_RESERVED,
};

enum hdmi_color_range {
	COLORRANGE_LIM,
	COLORRANGE_FUL,
	COLORRANGE_RESERVED,
};

struct parse_list {
	int val;
	const char *name;
};

static struct parse_list parse_cd [] = {
        {COLORDEPTH_24B, "8bit"},
        {COLORDEPTH_30B, "10bit"},
        {COLORDEPTH_36B, "12bit"},
        {COLORDEPTH_48B, "16bit"},
        {COLORDEPTH_RESERVED, NULL},
};

static struct parse_list parse_cs [] = {
        {COLORSPACE_RGB444, "rgb"},
        {COLORSPACE_YUV422, "422"},
        {COLORSPACE_YUV444, "444"},
        {COLORSPACE_YUV420, "420"},
        {COLORSPACE_RESERVED, NULL},
};

static struct parse_list parse_cr [] = {
        {COLORRANGE_LIM, "limit"},
        {COLORRANGE_FUL, "full"},
        {COLORRANGE_RESERVED, NULL},
};

/* this attribute contains a list of supported color spaces */
const char *g_cs_list_path;
/* this attribute contains the current color space */
const char *g_cs_path;

struct colorspace_t
{
	/* color space */
	/*enum hdmi_color_space*/int cs;
	/* color depth */
	/*enum hdmi_color_depth*/int cd;
	/* color range */
	/*enum hdmi_color_range*/int cr;
};

/* A list of color spaces supported by display */
static struct colorspace_t g_cs_supported [32];
static int g_cs_supported_size = 0;

struct cs_filter_t
{
	/* Regular expression for video mode name */
	regex_t rex;
	/* Color Space details */
	struct colorspace_t cs;
};
/* regex -> colorspace filters */
static struct cs_filter_t g_cs_filter [32];
/* Number of filters in the array */
static int g_cs_filter_size = 0;
/* Default color space */
static char *g_cs_default = 0;

static bool parse_str (const char *str, int *val, struct parse_list *list)
{
	int i;
	for (i = 0; list [i].name; i++)
		if (!strcmp (str, list [i].name)) {
			*val = list [i].val;
			return true;
		}

	return false;
}

static bool colorspace_parse (char *filt, struct colorspace_t *cs, bool reserved)
{
	if (reserved) {
		cs->cs = COLORSPACE_RESERVED;
		cs->cd = COLORDEPTH_RESERVED;
		cs->cr = COLORRANGE_RESERVED;
	}

	if (!filt)
		return false;

	char *cur_r, *cur, *tokens = filt;
	while ((cur = strtok_r (tokens, ",", &cur_r)) != NULL)
	{
		tokens = NULL;

		if (!parse_str (cur, &cs->cs, parse_cs) &&
		    !parse_str (cur, &cs->cd, parse_cd) &&
		    !parse_str (cur, &cs->cr, parse_cr))
			return false;
	}

	return true;
}

static const char *colorspace_str (struct colorspace_t *cs)
{
	static char tmp [32];
	int len = 0;

	int i;

	for (i = 0; parse_cs [i].name; i++)
		if (cs->cs == parse_cs [i].val) {
			len += snprintf (tmp + len, sizeof (tmp) - len - 1, "%s",
				parse_cs [i].name);
			break;
		}

	for (i = 0; parse_cd [i].name; i++)
		if (cs->cd == parse_cd [i].val) {
			len += snprintf (tmp + len, sizeof (tmp) - len - 1, "%s%s",
				len ? "," : "", parse_cd [i].name);
			break;
		}

	for (i = 0; parse_cr [i].name; i++)
		if (cs->cr == parse_cr [i].val) {
			len += snprintf (tmp + len, sizeof (tmp) - len - 1, "%s%s",
				len ? "," : "", parse_cr [i].name);
			break;
		}

	tmp [len] = 0;
	return tmp;
}

static bool colorspace_parse_filter (const char *csel)
{
	char *csel_dup = strdup (csel);
	char *cur_r, *cur, *tokens = csel_dup;
	while ((cur = strtok_r (tokens, spaces, &cur_r)) != NULL)
	{
		tokens = NULL;
		cur += strspn (cur, spaces);

		if (g_cs_filter_size >= ARRAY_SIZE (g_cs_filter)) {
			trace (1, "\tignoring excessive color space filter: %s\n", cur);
			continue;
		}
		struct cs_filter_t *csf = &g_cs_filter [g_cs_filter_size];

		char *val = strchr (cur, '=');
		if (!val) {
			trace (1, "\tinvalid color space selector: %s\n", csel);
			continue;
		}

		*val++ = 0;
		val += strspn (val, spaces);

		// cur is regexp for video mode
		// val is one of rgb, 444 or 420
		if (!colorspace_parse (val, &csf->cs, true)) {
			trace (1, "\tignoring invalid color space: %s\n", val);
			continue;
		}

		if (regcomp (&csf->rex, cur, REG_EXTENDED) != 0) {
			trace (1, "\tignoring bad regex: %s\n", cur);
			continue;
		}

		trace (2, "\t+ [%s] if mode matches %s\n", colorspace_str (&csf->cs), cur);
		g_cs_filter_size++;
	}

	free (csel_dup);

	return true;
}

bool colorspace_refresh ()
{
	g_cs_supported_size = 0;
	if (!g_cs_list_path || !g_cs_path)
		return false;

	char *list = sysfs_read (g_cs_list_path);
	if (!list)
		return false;

	trace (1, "loading available Color Spaces\n");

	char *cur_r, *cur, *tokens = list;
	while ((cur = strtok_r (tokens, spaces, &cur_r)) != NULL)
	{
		tokens = NULL;
		cur += strspn (cur, spaces);

		if (g_cs_supported_size > ARRAY_SIZE (g_cs_supported)) {
			trace (1, "\tignoring excessive supported color space: %s\n", cur);
			continue;
		}
		struct colorspace_t *cs = &g_cs_supported [g_cs_supported_size];

		if (!colorspace_parse (cur, cs, true)) {
			trace (1, "\tignoring invalid color space: %s\n", cur);
			continue;
		}

		trace (2, "\t+ %s\n", colorspace_str (cs));
		g_cs_supported_size++;
	}

	free (list);

	if (g_cs_default)
		free (g_cs_default);
	g_cs_default = sysfs_read (g_cs_path);

	return true;
}

static bool colorspace_supported (struct colorspace_t *cs)
{
	for (int i = 0; i < g_cs_supported_size; i++) {
		struct colorspace_t *scs = &g_cs_supported [i];

		if ((scs->cs != COLORSPACE_RESERVED) &&
		    (cs->cs != COLORSPACE_RESERVED) &&
		    (scs->cs != cs->cs))
			continue;

		if ((scs->cd != COLORDEPTH_RESERVED) &&
		    (cs->cd != COLORDEPTH_RESERVED) &&
		    (scs->cd != cs->cd))
			continue;

		if ((scs->cr != COLORRANGE_RESERVED) &&
		    (cs->cr != COLORRANGE_RESERVED) &&
		    (scs->cr != cs->cr))
			continue;

		return true;
	}

	return false;
}

bool colorspace_apply (const char *mode)
{
	if (!g_cs_list_path || !g_cs_path)
		return false;

	const char *cs_attr;

	/* default colorspace parameters */
	struct colorspace_t def_cs = {COLORSPACE_YUV444, COLORDEPTH_24B, COLORRANGE_FUL};
	colorspace_parse (g_cs_default, &def_cs, false);

	/* current colorspace setting */
	struct colorspace_t cur_cs = def_cs;
	char *cur_cs_str = sysfs_read (g_cs_path);
	colorspace_parse (cur_cs_str, &cur_cs, false);
	free (cur_cs_str);

	for (int i = 0; i < g_cs_filter_size; i++) {
		struct cs_filter_t *csf = &g_cs_filter [i];

		regmatch_t match [1];
		if (regexec (&csf->rex, mode, 1, match, 0) == REG_NOMATCH)
			continue;
		// must match whole line
		if (match [0].rm_so != 0 || match [0].rm_eo != strlen (mode))
			continue;

		if (!colorspace_supported (&csf->cs)) {
			trace (2, "Not using color space %s because not supported\n",
				colorspace_str (&csf->cs));
			continue;
		}

		if (csf->cs.cs != COLORSPACE_RESERVED)
			cur_cs.cs = csf->cs.cs;
		if (csf->cs.cd != COLORDEPTH_RESERVED)
			cur_cs.cd = csf->cs.cd;
		if (csf->cs.cr != COLORRANGE_RESERVED)
			cur_cs.cr = csf->cs.cr;
		goto apply;
	}

	/* use default colorspace if no match found */
	cur_cs = def_cs;

apply:
	cs_attr = colorspace_str (&cur_cs);
	trace (1, "Setting color space to %s\n", cs_attr);
	return sysfs_write (g_cs_path, cs_attr) == 0;
}

void colorspace_init ()
{
	g_cs_list_path = cfg_get_str ("cs.list.path", NULL);
	if (!g_cs_list_path)
		return;

	g_cs_path = cfg_get_str ("cs.path", NULL);
	if (!g_cs_path)
		return;

	const char *cs_select = cfg_get_str ("cs.select", NULL);
	if (!cs_select)
		return;

	trace (1, "loading Color Space selector\n");

	if (!colorspace_parse_filter (cs_select))
		return;

	colorspace_refresh ();
}

void colorspace_fini ()
{
	for (int i = 0; i < g_cs_filter_size; i++) {
		struct cs_filter_t *csf = &g_cs_filter [i];
		regfree (&csf->rex);
	}

	g_cs_filter_size = 0;

	if (g_cs_default)
		free (g_cs_default);

	g_cs_list_path = g_cs_path = NULL;
}
