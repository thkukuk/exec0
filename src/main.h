// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <libeconf.h>

typedef struct {
  char *name;
  char *description;
  char *chdir;
  char *group;
  char *user;
  char *executable;
  bool preserve_environment;
  char *whitelist_env;
  bool verbose;
  char *background_color;
} config_t;

/* Upper bound on the length of a --background argument / "background" config value we accept: generous
   enough for any real SGR background parameter (e.g. "48;2;255;255;255" for 24-bit color, 17 bytes), while
   keeping the worst-case growth of ptybroker.c's output filtering bounded. */
#define BACKGROUND_COLOR_MAX_LEN 32

extern int main_su(int argc, char *argv[]);
extern int main_sudo(int argc, char *argv[]);
extern int main_pkexec(int argc, char *argv[]);

extern char *find_program_in_path(const char *program);
extern bool is_env_assignment(const char *arg);
extern bool is_env_list(const char *arg);
extern int whitelist_add(char **list, const char *name);
extern bool looks_like_ansi_color_code(const char *str);


// XXX extern struct config_t *struct_config_free(config_t *cfg);
extern econf_err read_config(const char *name, config_t *cfg);
