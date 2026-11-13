// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "basics.h"
#include "main.h"
#include "ptybroker.h"

static void
usage_sudo(FILE *fp)
{
  fprintf(fp,
	  "usage: sudo [-u user] [-g group] [-i | -s] [-H] [-E | --preserve-env=list]\n"
	  "            [-D directory] [-n] [VAR=value]... [COMMAND [ARGUMENT...]]\n"
	  "usage: sudo -h | -V | -v | -k | -K\n");
}

static void
help_sudo(void)
{
  usage_sudo(stdout);
  printf("\nExecute a command as another user, via PTYBroker.\n\n"
	 "  -D, --chdir=directory       change the working directory before running command\n"
	 "  -E, --preserve-env          preserve user environment when running command\n"
	 "      --preserve-env=list     preserve specific, comma-separated environment variables\n"
	 "  -g, --group=group           run command as the specified group\n"
	 "  -H, --set-home              set HOME to the target user's home directory\n"
	 "  -h, --help                  display this help message and exit\n"
	 "  -i, --login                 run a login shell as the target user\n"
	 "  -K, --remove-timestamp      accepted for compatibility; nothing to remove\n"
	 "  -k, --reset-timestamp       accepted for compatibility, without effect\n"
	 "  -n, --non-interactive       accepted for compatibility, without effect\n"
	 "  -s, --shell                 run a shell as the target user\n"
	 "  -u, --user=user             run command as the specified user (default: root)\n"
	 "  -V, --version               display version information and exit\n"
	 "  -v, --validate              accepted for compatibility, without effect\n");
}

/* Emulates sudo(1)'s command line and defaults as closely as reasonably possible on top of exec0's
   PTYBroker plumbing: authentication (via polkit) and environment/session handling are the broker's
   job (see acquire_shell_pty() and build_environment() in ptybroker.c); what's left to do here is
   turning sudo's own command line into the equivalent PTYBroker request.

   sudo's timestamp/ticket cache (-k/-K/-v/-N) has no equivalent: the broker authenticates (via
   polkit) on every single invocation, so those flags are accepted for compatibility but are no-ops
   beyond their effect on whether a command may/must follow (see sudo(1)). Likewise, -l/--list,
   -e/--edit (sudoedit) and -b/--background have no sensible equivalent via the PTYBroker and are
   rejected outright rather than silently ignored. */
int
main_sudo(int argc, char *argv[])
{
  _cleanup_free_ char *resolved_path = NULL;
  _cleanup_free_ char *argv0 = NULL;
  _cleanup_free_ char *quoted_cmnd = NULL;
  _cleanup_free_ char *whitelist = NULL;
  config_t args = {};
  char *opt_user = "root";
  char *opt_preserve_list = NULL;
  bool opt_login = false;
  bool opt_shell_mode = false;
  bool opt_set_home = false;
  bool opt_preserve_all = false;
  bool opt_reset_timestamp = false;
  bool opt_remove_timestamp = false;
  bool opt_validate = false;
  struct passwd *pw;
  unsigned n;
  int r;
  econf_err cfg_err;

  if (argc < 1)
    return -EINVAL;

  /* Load /etc/exec0/sudo.conf (or /usr/share/exec0/sudo.conf) first, so its settings act as
     defaults that the command line parsed below can still override. */
  cfg_err = read_config("sudo", &args);
  if (cfg_err != ECONF_SUCCESS && cfg_err != ECONF_NOFILE)
    fprintf(stderr, "sudo: failed to read configuration: %s\n", econf_errString(cfg_err));

  for (n = 1; n < (unsigned) argc; n++)
    {
      char *arg = argv[n];

      if (streq(arg, "--"))
	{
	  n++;
	  break;
	}
      else if (streq(arg, "-h") || streq(arg, "--help"))
	{
	  help_sudo();
	  return 0;
	}
      else if (streq(arg, "-V") || streq(arg, "--version"))
	{
	  printf("sudo (%s) %s\n", PACKAGE, VERSION);
	  return 0;
	}
      else if (streq(arg, "-u") || streq(arg, "--user"))
	{
	  if (++n >= (unsigned) argc)
	    {
	      fprintf(stderr, "sudo: option requires an argument -- 'u'\n");
	      usage_sudo(stderr);
	      return -EINVAL;
	    }
	  opt_user = argv[n];
	}
      else if (strneq(arg, "--user=", 7))
	opt_user = arg + 7;
      else if (streq(arg, "-g") || streq(arg, "--group"))
	{
	  if (++n >= (unsigned) argc)
	    {
	      fprintf(stderr, "sudo: option requires an argument -- 'g'\n");
	      usage_sudo(stderr);
	      return -EINVAL;
	    }
	  args.group = argv[n];
	}
      else if (strneq(arg, "--group=", 8))
	args.group = arg + 8;
      else if (streq(arg, "-D") || streq(arg, "--chdir"))
	{
	  if (++n >= (unsigned) argc)
	    {
	      fprintf(stderr, "sudo: option requires an argument -- 'D'\n");
	      usage_sudo(stderr);
	      return -EINVAL;
	    }
	  args.chdir = argv[n];
	}
      else if (strneq(arg, "--chdir=", 8))
	args.chdir = arg + 8;
      else if (streq(arg, "-i") || streq(arg, "--login"))
	opt_login = true;
      else if (streq(arg, "-s") || streq(arg, "--shell"))
	opt_shell_mode = true;
      else if (streq(arg, "-H") || streq(arg, "--set-home"))
	opt_set_home = true;
      else if (streq(arg, "-E") || streq(arg, "--preserve-env"))
	opt_preserve_all = true;
      else if (strneq(arg, "--preserve-env=", 15))
	opt_preserve_list = arg + 15;
      else if (strneq(arg, "-E", 2) && arg[2] != '\0')
	opt_preserve_list = arg + 2;
      else if (streq(arg, "-n") || streq(arg, "--non-interactive"))
	/* Authentication is always driven interactively by polkit on our own terminal (see
	   polkit_agent_open() in ptybroker.c); there is nothing to switch off, but the flag is
	   accepted for command-line compatibility. */
	;
      else if (streq(arg, "-k") || streq(arg, "--reset-timestamp"))
	opt_reset_timestamp = true;
      else if (streq(arg, "-K") || streq(arg, "--remove-timestamp"))
	opt_remove_timestamp = true;
      else if (streq(arg, "-v") || streq(arg, "--validate"))
	opt_validate = true;
      else if (streq(arg, "--verbose"))
	args.verbose = true;
      else if (streq(arg, "-e") || streq(arg, "--edit") ||
	       streq(arg, "-l") || streq(arg, "--list") ||
	       streq(arg, "-b") || streq(arg, "--background"))
	{
	  fprintf(stderr, "sudo: '%s' is not supported via the PTYBroker.\n", arg);
	  return -EOPNOTSUPP;
	}
      else if (arg[0] == '-' && arg[1] != '\0')
	{
	  fprintf(stderr, "sudo: invalid option -- '%s'\n", arg + (arg[1] == '-' ? 2 : 1));
	  usage_sudo(stderr);
	  return -EINVAL;
	}
      else
	break; /* first non-option argument: a VAR=value assignment, or the command */
    }

  if (opt_login && opt_shell_mode)
    {
      fprintf(stderr, "sudo: you may not specify both the -i and -s options\n");
      usage_sudo(stderr);
      return -EINVAL;
    }
  if (opt_login && opt_preserve_all)
    {
      fprintf(stderr, "sudo: you may not specify both the -i and -E options\n");
      usage_sudo(stderr);
      return -EINVAL;
    }
  if (opt_remove_timestamp && opt_reset_timestamp)
    {
      fprintf(stderr, "sudo: only one of the -K or -k options may be specified\n");
      usage_sudo(stderr);
      return -EINVAL;
    }
  if (opt_preserve_list && opt_preserve_list[0] == '\0')
    {
      fprintf(stderr, "sudo: --preserve-env requires a non-empty list\n");
      usage_sudo(stderr);
      return -EINVAL;
    }

  // sudo without arguments bails out with an error
  if (n >= (unsigned) argc && !opt_login && !opt_shell_mode && !opt_validate &&
      !opt_reset_timestamp && !opt_remove_timestamp)
    {
      fprintf(stderr, "sudo: a command is required\n");
      usage_sudo(stderr);
      return -EINVAL;
    }

  if (opt_preserve_list && !opt_preserve_all)
    {
      whitelist = strdup(opt_preserve_list);
      if (!whitelist)
	{
	  fprintf(stderr, "sudo: out of memory\n");
	  return -ENOMEM;
	}
    }

  /* Anything left that looks like VAR=value is an environment variable assignment for the command,
     same as sudo(1): set it in our own environment (so getpwnam()/build_environment() downstream,
     and -E's "preserve everything" below, pick it up) and, unless the whole environment is already
     being preserved, whitelist it explicitly so build_environment() forwards it. */
  while (n < (unsigned) argc && is_env_assignment(argv[n]))
    {
      char *eq = strchr(argv[n], '=');

      *eq = '\0';
      r = setenv(argv[n], eq + 1, 1);
      if (r == 0 && !opt_preserve_all)
	r = whitelist_add(&whitelist, argv[n]);
      *eq = '=';
      if (r < 0)
	{
	  fprintf(stderr, "sudo: out of memory\n");
	  return -ENOMEM;
	}

      n++;
    }

  bool have_cmd = n < (unsigned) argc;

  if (opt_remove_timestamp)
    {
      if (have_cmd)
	{
	  fprintf(stderr, "sudo: -K/--remove-timestamp cannot be used with a command\n");
	  return -EINVAL;
	}
      return 0;
    }
  if (opt_validate)
    {
      if (have_cmd)
	{
	  fprintf(stderr, "sudo: -v/--validate cannot be used with a command\n");
	  return -EINVAL;
	}
      return 0;
    }
  /* "sudo -k" by itself just invalidates the (nonexistent, as far as we're concerned) timestamp and
     exits; combined with a command, or with -i/-s, it is a no-op that falls through to running it. */
  if (opt_reset_timestamp && !have_cmd && !opt_login && !opt_shell_mode)
    return 0;

  /* 'opt_user' points into argv[] (or the "root" literal), so it stays valid for the life of the
     process, unlike 'pw', which is backed by a static buffer getpwnam(3)/getpwuid(3) share and that
     build_environment() in ptybroker.c will clobber via its own getpwuid() call. */
  args.user = opt_user;

  errno = 0;
  pw = getpwnam(opt_user);
  if (!pw)
    {
      r = errno ? -errno : -ENOENT;
      fprintf(stderr, "sudo: user %s does not exist\n", opt_user);
      return r;
    }

  if (opt_set_home)
    {
      if (setenv("HOME", pw->pw_dir, 1) < 0)
	{
	  fprintf(stderr, "sudo: out of memory\n");
	  return -ENOMEM;
	}
      if (!opt_preserve_all)
	{
	  r = whitelist_add(&whitelist, "HOME");
	  if (r < 0)
	    {
	      fprintf(stderr, "sudo: out of memory\n");
	      return r;
	    }
	}
    }

  if (opt_preserve_all)
    args.preserve_environment = true;
  else if (whitelist)
    args.whitelist_env = whitelist;

  /* Like su -l / pkexec's default: only -i switches the working directory to the target user's home;
     plain sudo, like sudo(1) itself, stays in the caller's current directory (exec0's own default,
     see working_directory_to_json() in ptybroker.c) unless -D/--chdir overrides it explicitly, which
     was already stored directly into args.chdir above. */
  if (opt_login && !args.chdir)
    args.chdir = pw->pw_dir;

  /* sudo(1) runs a shell, rather than executing PROGRAM directly, for -i and -s. Unlike sudo(1), there
     is no "implied shell" for a command-less invocation in general: that case was already rejected
     above unless the only thing left on the command line was a VAR=value assignment (no -i/-s either),
     in which case fall through to the same shell-building code below as -s, i.e. a non-login shell. */
  bool run_shell = opt_login || opt_shell_mode;

  char **cmdline;

  if (run_shell || !have_cmd)
    {
      _cleanup_free_ char *resolved_shell = NULL;
      char *shell;

      if (opt_login)
	shell = (pw->pw_shell && pw->pw_shell[0]) ? pw->pw_shell : "/bin/sh";
      else
	{
	  char *envshell = getenv("SHELL");
	  shell = (envshell && envshell[0]) ? envshell :
	    ((pw->pw_shell && pw->pw_shell[0]) ? pw->pw_shell : "/bin/sh");
	}

      if (shell[0] != '/')
	{
	  resolved_shell = find_program_in_path(shell);
	  if (!resolved_shell)
	    {
	      fprintf(stderr, "sudo: cannot run shell %s: %s\n", shell, strerror(ENOENT));
	      return -ENOENT;
	    }
	  shell = resolved_shell;
	}
      if (access(shell, X_OK) != 0)
	{
	  r = -errno;
	  fprintf(stderr, "sudo: error accessing %s: %s\n", shell, strerror(-r));
	  return r;
	}

      args.executable = shell;

      /* Like run_shell() in util-linux's su(1): the broker executes 'executable' directly, while the
	 first element of 'commandLine' is only ever used as argv[0], so login-shell semantics are
	 requested the same way su does it, by prefixing argv[0] with a dash. */
      if (opt_login)
	r = asprintf(&argv0, "-%s", basename(shell));
      else
	{
	  argv0 = strdup(basename(shell));
	  r = argv0 ? 0 : -1;
	}
      if (r < 0)
	{
	  fprintf(stderr, "sudo: out of memory\n");
	  return -ENOMEM;
	}

      size_t n_cmdline = 1 + (have_cmd ? 2 : 0) + 1;
      _cleanup_free_ char **built = calloc(n_cmdline, sizeof(char *));
      if (!built)
	{
	  fprintf(stderr, "sudo: out of memory\n");
	  return -ENOMEM;
	}

      size_t i = 0;
      built[i++] = argv0;
      if (have_cmd)
	{
	  quoted_cmnd = quote_shell_command(argv + n);
	  if (!quoted_cmnd)
	    {
	      fprintf(stderr, "sudo: out of memory\n");
	      return -ENOMEM;
	    }
	  built[i++] = (char *) "-c";
	  built[i++] = quoted_cmnd;
	}
      built[i] = NULL;

      return run_pty_session(&args, built);
    }

  /* Plain "sudo COMMAND [ARGUMENT...]": run it directly, same as exec0/pkexec do. */
  char *path = argv[n];
  if (path[0] != '/')
    {
      resolved_path = find_program_in_path(path);
      if (!resolved_path)
	{
	  fprintf(stderr, "sudo: %s: command not found\n", path);
	  return -ENOENT;
	}
      path = resolved_path;
    }
  if (access(path, F_OK) != 0)
    {
      r = -errno;
      fprintf(stderr, "sudo: error accessing %s: %s\n", path, strerror(-r));
      return r;
    }

  argv[n] = path;
  cmdline = argv + n;

  return run_pty_session(&args, cmdline);
}
