// driver test program for cfg_parse

#include "cfg_parse.h"

#include <stdio.h>

int main(int argc, char **argv)
{
  // Pointer to a cfg_struct structure
  struct cfg_struct *cfg;

  // Initialize config struct
  cfg = cfg_init();

  // Specifying some defaults
  cfg_set(cfg,"KEY","VALUE");
  cfg_set(cfg,"KEY_A","DEFAULT_VALUE_A");

  // "Required" file
  if (cfg_load(cfg,"config.ini") < 0)
  {
    fprintf(stderr,"Unable to load cfg.ini\n");
    return -1;
  }

  // Several "optional" files can be added as well
  //  Each subsequent call upserts values already in
  //  the cfg structure.
  cfg_load(cfg,"/usr/local/etc/config.ini");
  cfg_load(cfg,"~/.config");

  // Retrieve the value for key INFINITY, and print
  printf("INFINITY = %s\n",cfg_get(cfg,"INFINITY"));

  // Retrieve the value for key "KEY", and print
  printf("KEY = %s\n",cfg_get(cfg,"KEY"));

  // Delete the key-value pair for "DELETE_ME"
  cfg_delete(cfg,"DELETE_ME");

  // Dump cfg-struct to disk.
  cfg_save(cfg,"config_new.ini");

  // All done, clean up.
  cfg_free(cfg);

  return 0;
}
