// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

/* Command-line derived settings that control how acquire_shell_pty() talks to the broker; filled in by
   whichever main_*() mode is active (exec0, pkexec, su) and threaded through explicitly rather than kept as
   global state. */
typedef struct {
  char *name;
  char *chdir;
  char *group;
  char *user;
  char *executable;
  bool preserve_environment;
  char *whitelist_env;
  bool verbose;
} Exec0Args;

int run_pty_session(const Exec0Args *args, char **cmdline);
