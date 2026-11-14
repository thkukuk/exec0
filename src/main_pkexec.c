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
usage_pkexec(FILE *fp)
{
  fprintf(fp,
	  "pkexec [--keep-cwd] [--user username] [PROGRAM] [ARGUMENTS...]\n"
	  "pkexec  --version | --help\n");
}

static void
help_pkexec(void)
{
  usage_pkexec(stdout);
  printf("\nExecute a command as another user through systemd-ptybrokerd.\n\n"
	 "      --disable-internal-agent  Accepted for command-line compatibility; has no effect.\n"
         "      --help                    Display this help message and exit\n"
	 "      --keep-cwd                Stay in current directroy\n"
         "  -u, --user <user>             Run command as the specified user (default: root)\n"
         "  -v, --verbose                 Print verbose information\n"
         "      --version                 Show version and exit\n");
}

int
main_pkexec(int argc, char *argv[])
{
  config_t cfg = {};
  char *opt_user = "root";
  bool opt_keep_cwd = false;
  struct passwd *pw;
  int n;
  int r;
  econf_err cfg_err;

  if (argc < 1)
    return -EINVAL;

  cfg_err = read_config("pkexec", &cfg);
  if (cfg_err != ECONF_SUCCESS && cfg_err != ECONF_NOFILE)
    fprintf(stderr, "pkexec: failed to read configuration: %s\n", econf_errString(cfg_err));

  for (n = 1; n < argc; n++)
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
	  if (n >= argc)
	    {
	      fprintf(stderr, "pkexec: option requires an argument -- 'u'\n");
	      usage_pkexec(stderr);
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
      else if (argv[n][0] == '-')
        {
          fprintf(stderr, "pkexec: invalid option -- '%s'\n", argv[n] + (argv[n][1] == '-' ? 2 : 1));
          usage_pkexec(stderr);
          return -EINVAL;
        }
      else
	break;
    }

  r = lookup_user_and_group(opt_user, NULL, &pw);
  if (r < 0)
    return r;

  cfg.user = opt_user;

  if (!opt_keep_cwd)
    {
      cfg.chdir = strdup(pw->pw_dir);
      if (!cfg.chdir)
	{
	  fprintf(stderr, "Out of memory.\n");
	  return -ENOMEM;
	}
    }

  // No PROGRAM to run, use standard shell
  if (n >= argc)
    return run_pty_session(&cfg, NULL);

  // Anything left over is PROGRAM [ARGUMENTS...]
  if (argv[n][0] != '/')
    {
      _cleanup_free_ char *resolved_path = find_program_in_path(argv[n]);
      if (!resolved_path)
	{
	  fprintf(stderr, "Command not found: '%s'\n", argv[n]);
	  return -ENOENT;
	}
      argv[n] = TAKE_PTR(resolved_path);
    }
  else if (access(argv[n], F_OK) != 0)
    {
      r = -errno;
      fprintf(stderr, "Error accessing '%s': %s\n", argv[n], strerror(-r));
      return r;
    }

  return run_pty_session(&cfg, argv + n);
}
