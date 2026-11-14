// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "basics.h"
#include "main.h"
#include "ptybroker.h"

static void
help_pkexec(void)
{
  printf("pkexec --version |\n"
	 "       --help |\n"
	 "       --disable-internal-agent |\n"
	 "       [-v | --verbose] [--keep-cwd] [--user username] [PROGRAM] [ARGUMENTS...]\n");
}

int
main_pkexec(int argc, char *argv[])
{
  _cleanup_free_ char *resolved_path = NULL;
  _cleanup_free_ char *chdir_home = NULL;
  config_t cfg = {};
  char *opt_user = "root";
  bool opt_keep_cwd = false;
  char **cmdline = NULL;
  struct passwd *pw;
  unsigned n;
  int r;
  econf_err cfg_err;

  if (argc < 1)
    return -EINVAL;

  cfg_err = read_config("pkexec", &cfg);
  if (cfg_err != ECONF_SUCCESS && cfg_err != ECONF_NOFILE)
    fprintf(stderr, "pkexec: failed to read configuration: %s\n", econf_errString(cfg_err));

  for (n = 1; n < (unsigned) argc; n++)
    {
      if (streq(argv[n], "--help"))
	{
	  help_pkexec();
	  return 0;
	}
      else if (streq(argv[n], "--version"))
	{
	  printf("pkexec (%s) %s\n", PACKAGE, VERSION);
	  return 0;
	}
      else if (streq(argv[n], "--user") || streq(argv[n], "-u"))
	{
	  n++;
	  if (n >= (unsigned) argc)
	    {
	      help_pkexec();
	      return -EINVAL;
	    }
	  opt_user = argv[n];
	}
      else if (streq(argv[n], "--disable-internal-agent"))
	{
	  /* Our internal agent is always a best-effort pkttyagent spawned for this very process (see
	     polkit_agent_open()); there currently is nothing to disable, but the flag is accepted for
	     command-line compatibility. */
	}
      else if (streq(argv[n], "--keep-cwd"))
	opt_keep_cwd = true;
      else if (streq(argv[n], "-v") || streq(argv[n], "--verbose"))
	cfg.verbose = true;
      else
	break;
    }

  r = lookup_user_and_group(opt_user, NULL, &pw);
  if (r < 0)
    return r;

  /* 'opt_user' points into argv[] (or the "root" literal), so it stays valid for the life of the process,
     unlike 'pw', which is backed by a static buffer getpwnam(3)/getpwuid(3) share and that build_environment()
     in ptybroker.c will clobber via its own getpwuid() call. */
  cfg.user = opt_user;

  /* Like pkexec, default to the target user's home directory; --keep-cwd keeps ours (exec0's own default,
     see working_directory_to_json() in ptybroker.c). */
  if (!opt_keep_cwd)
    {
      chdir_home = strdup(pw->pw_dir);
      if (!chdir_home)
	{
	  fprintf(stderr, "Out of memory.\n");
	  return -ENOMEM;
	}
      cfg.chdir = chdir_home;
    }

  /* Anything left over is PROGRAM [ARGUMENTS...]; if there is none, leave cmdline NULL so the broker runs
     the target user's login shell, same as plain exec0 does for the caller's own shell. */
  if (n < (unsigned) argc)
    {
      char *path = argv[n];

      if (path[0] != '/')
	{
	  resolved_path = find_program_in_path(path);
	  if (!resolved_path)
	    {
	      fprintf(stderr, "Cannot run program %s: %s\n", path, strerror(ENOENT));
	      return -ENOENT;
	    }
	  path = resolved_path;
	}

      if (access(path, F_OK) != 0)
	{
	  r = -errno;
	  fprintf(stderr, "Error accessing %s: %s\n", path, strerror(-r));
	  return r;
	}

      argv[n] = path;
      cmdline = argv + n;
    }

  return run_pty_session(&cfg, cmdline);
}
