// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include <errno.h>
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
help_su(void)
{
  printf("su [options] [-] [<user> [<argument>...]]\n\n"
	 "Change the effective user ID and group ID to that of <user>, via PTYBroker.\n"
	 "A mere - implies --login. If <user> is not given, root is assumed.\n\n"
	 "  -c, --command <command>              pass <command> to the shell with -c\n"
	 "      --session-command <command>      same as -c\n"
	 "  -f, --fast                           pass -f to the shell\n"
	 "  -g, --group <group>                  specify the primary group\n"
	 "  -, -l, --login                       make the shell a login shell\n"
	 "  -m, -p, --preserve-environment       do not reset environment variables\n"
	 "  -s, --shell <shell>                  run <shell> instead of the target user's default\n"
	 "  -w, --whitelist-environment <list>   forward the given, comma-separated variables\n"
	 "  -P, --pty, -T, --no-pty              accepted for compatibility, without effect\n"
	 "  -v, --verbose                        show additional information\n"
	 "  -h, --help                           show this help\n"
	 "  -V, --version                        show version\n");
}

int
main_su(int argc, char *argv[])
{
  _cleanup_free_ char *resolved_shell = NULL;
  _cleanup_free_ char *chdir_home = NULL;
  config_t cfg = {};
  char *opt_user = "root";
  char *opt_shell = NULL;
  char *opt_command = NULL;
  bool opt_login = false;
  bool opt_fast = false;
  struct passwd *pw;
  unsigned n;
  int r;
  econf_err cfg_err;

  if (argc < 1)
    return -EINVAL;

  /* Load /etc/exec0/su.conf (or /usr/share/exec0/su.conf) first, so its settings act as defaults
     that the command line parsed below can still override. */
  cfg_err = read_config("su", &cfg);
  if (cfg_err != ECONF_SUCCESS && cfg_err != ECONF_NOFILE)
    fprintf(stderr, "su: failed to read configuration: %s\n", econf_errString(cfg_err));

  for (n = 1; n < (unsigned) argc; n++)
    {
      if (streq(argv[n], "-h") || streq(argv[n], "--help"))
	{
	  help_su();
	  return 0;
	}
      else if (streq(argv[n], "-V")  || streq(argv[n], "--version"))
	{
	  printf("su (%s) %s\n", PACKAGE, VERSION);
	  return 0;
	}
      else if (streq(argv[n], "-") || streq(argv[n], "-l") || streq(argv[n], "--login"))
	opt_login = true;
      else if (streq(argv[n], "-f") || streq(argv[n], "--fast"))
	opt_fast = true;
      else if (streq(argv[n], "-m") || streq(argv[n], "-p") || streq(argv[n], "--preserve-environment"))
	cfg.preserve_environment = true;
      else if (streq(argv[n], "-c") || streq(argv[n], "--command") || streq(argv[n], "--session-command"))
	{
	  n++;
	  if (n >= (unsigned) argc)
	    {
	      help_su();
	      return -EINVAL;
	    }
	  opt_command = argv[n];
	}
      else if (streq(argv[n], "-s") || streq(argv[n], "--shell"))
	{
	  n++;
	  if (n >= (unsigned) argc)
	    {
	      help_su();
	      return -EINVAL;
	    }
	  opt_shell = argv[n];
	}
      else if (streq(argv[n], "-g") || streq(argv[n], "--group"))
	{
	  n++;
	  if (n >= (unsigned) argc)
	    {
	      help_su();
	      return -EINVAL;
	    }
	  cfg.group = argv[n];
	}
      else if (streq(argv[n], "-w") || streq(argv[n], "--whitelist-environment"))
	{
	  n++;
	  if (n >= (unsigned) argc)
	    {
	      help_su();
	      return -EINVAL;
	    }
	  cfg.whitelist_env = argv[n];
	}
      else if (streq(argv[n], "-P") || streq(argv[n], "--pty") ||
	       streq(argv[n], "-T") || streq(argv[n], "--no-pty"))
	/* exec0 always proxies a pty allocated by the broker; nothing to toggle. */
	;
      else if (streq(argv[n], "-v") || streq(argv[n], "--verbose"))
	cfg.verbose = true;
      else if (streq(argv[n], "-G") || streq(argv[n], "--supp-group"))
	{
	  fprintf(stderr, "su: supplementary groups are not supported via the PTYBroker.\n");
	  return -EOPNOTSUPP;
	}
      else if (argv[n][0] == '-' && argv[n][1] != '\0')
	{
	  fprintf(stderr, "su: unrecognized option '%s'\n"
		  "Try 'su --help' for more information.\n", argv[n]);
	  return -EINVAL;
	}
      else
	break; /* first non-option argument: the target user */
    }

  if (n < (unsigned) argc)
    opt_user = argv[n++];

  /* 'opt_user' points into argv[] (or the "root" literal), so it stays valid for the life of the process,
     unlike 'pw', which is backed by a static buffer getpwnam(3)/getpwuid(3) share and that build_environment()
     in ptybroker.c will clobber via its own getpwuid() call. */
  cfg.user = opt_user;

  errno = 0;
  pw = getpwnam(opt_user);
  if (!pw)
    {
      r = errno ? -errno : -ENOENT;
      fprintf(stderr, "su: user %s does not exist\n", opt_user);
      return r;
    }

  /* Unlike pkexec, su by default stays in the caller's current directory (exec0's own default, see
     working_directory_to_json() in ptybroker.c); only --login switches to the target user's home, same as
     pkexec always does. */
  if (opt_login)
    {
      chdir_home = strdup(pw->pw_dir);
      if (!chdir_home)
	{
	  fprintf(stderr, "Out of memory.\n");
	  return -ENOMEM;
	}
      cfg.chdir = chdir_home;
    }

  char *shell = opt_shell ?: ((pw->pw_shell && pw->pw_shell[0]) ? pw->pw_shell : "/bin/sh");
  if (shell[0] != '/')
    {
      resolved_shell = find_program_in_path(shell);
      if (!resolved_shell)
	{
	  fprintf(stderr, "su: cannot run shell %s: %s\n", shell, strerror(ENOENT));
	  return -ENOENT;
	}
      shell = resolved_shell;
    }
  if (access(shell, X_OK) != 0)
    {
      r = -errno;
      fprintf(stderr, "su: error accessing %s: %s\n", shell, strerror(-r));
      return r;
    }

  /* Like run_shell() in util-linux's su(1): the broker executes 'executable' directly (see the 'executable'
     field in config_t / "executable" in ptybroker.c), while the first element of 'commandLine' is only ever
     used as argv[0], so login-shell semantics are requested the same way su does it, by prefixing argv[0]
     with a dash. */
  cfg.executable = shell;

  _cleanup_free_ char *argv0 = NULL;
  if (opt_login)
    r = asprintf(&argv0, "-%s", basename(shell));
  else
    {
      argv0 = strdup(basename(shell));
      r = argv0 ? 0 : -1;
    }
  if (r < 0)
    {
      fprintf(stderr, "Out of memory.\n");
      return -ENOMEM;
    }

  size_t n_additional = (unsigned) argc - n;
  size_t n_cmdline = 1 + opt_fast + (opt_command ? 2 : 0) + n_additional + 1;
  _cleanup_free_ char **cmdline = calloc(n_cmdline, sizeof(char *));
  if (!cmdline)
    {
      fprintf(stderr, "Out of memory.\n");
      return -ENOMEM;
    }

  size_t i = 0;
  cmdline[i++] = argv0;
  if (opt_fast)
    cmdline[i++] = (char *) "-f";
  if (opt_command)
    {
      cmdline[i++] = (char *) "-c";
      cmdline[i++] = opt_command;
    }
  for (size_t j = 0; j < n_additional; j++)
    cmdline[i++] = argv[n + j];
  cmdline[i] = NULL;

  return run_pty_session(&cfg, cmdline);
}
