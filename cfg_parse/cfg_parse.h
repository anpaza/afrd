/* config file parser */
/* Greg Kennedy 2012 */

#ifndef CFG_STRUCT_H_
#define CFG_STRUCT_H_

#define CFG_MAX_LINE 1024

struct cfg_struct;

/* Create a cfg_struct */
struct cfg_struct * cfg_init();

/* Free a cfg_struct */
void cfg_free(struct cfg_struct *);


/* Load into cfg from a file */
int cfg_load(struct cfg_struct *, const char *);

/* Save complete cfg to file */
int cfg_save(struct cfg_struct *, const char *);


/* Get value from cfg_struct by key */
const char * cfg_get(struct cfg_struct *, const char *);

/* Set key,value in cfg_struct */
void cfg_set(struct cfg_struct *, const char *, const char *);

/* Delete key (+value) from cfg_struct */
void cfg_delete(struct cfg_struct *, const char *);

#endif
