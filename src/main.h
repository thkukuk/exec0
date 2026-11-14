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

extern int main_su(int argc, char *argv[]);
extern int main_sudo(int argc, char *argv[]);
extern int main_pkexec(int argc, char *argv[]);

extern char *find_program_in_path(const char *program);
extern bool is_env_assignment(const char *arg);
extern bool is_env_list(const char *arg);
extern int whitelist_add(char **list, const char *name);
extern char *quote_shell_command(char **cfg);


// XXX extern struct config_t *struct_config_free(config_t *cfg);
extern econf_err read_config(const char *name, config_t *cfg);
