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
#include "ptybroker.h"

// Looks up 'program' in $PATH, the way a shell (or execvp(3)) would.
static char *
find_program_in_path(const char *program)
{
  const char *path_env = getenv("PATH") ?: "/usr/bin:/bin";
  _cleanup_free_ char *paths = NULL;

  paths = strdup(path_env);
  if (!paths)
    return NULL;

  char *saveptr = NULL;
  for (char *dir = strtok_r(paths, ":", &saveptr); dir; dir = strtok_r(NULL, ":", &saveptr))
    {
      _cleanup_free_ char *candidate = NULL;

      if (asprintf(&candidate, "%s/%s", dir[0] ? dir : ".", program) < 0)
	return NULL;

      if (access(candidate, X_OK) == 0)
	return TAKE_PTR(candidate);
    }

  return NULL;
}

static void
help_pkexec(void)
{
  printf("pkexec --version |\n"
	 "       --help |\n"
	 "       --disable-internal-agent |\n"
	 "       [-v | --verbose] [--keep-cwd] [--user username] [PROGRAM] [ARGUMENTS...]\n");
}

/* Emulates pkexec(1)'s command line and defaults as closely as possible, but on top of exec0's PTYBroker
   plumbing: acquire_shell_pty() already takes care of authentication (a pkttyagent is spawned on our
   terminal so the broker's polkit check can prompt for a password, see polkit_agent_open()) and of the
   environment handed to the session (see build_environment() in ptybroker.c). What's left to do here is
   turning pkexec's own command line into the equivalent exec0 request: who to run as, where to start, and
   what to run. */
static int
main_pkexec(int argc, char *argv[])
{
  _cleanup_free_ char *resolved_path = NULL;
  _cleanup_free_ char *chdir_home = NULL;
  Exec0Args args = {};
  char *opt_user = "root";
  bool opt_keep_cwd = false;
  char **cmdline = NULL;
  struct passwd *pw;
  unsigned n;
  int r;

  if (argc < 1)
    return -EINVAL;

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
	args.verbose = true;
      else
	break;
    }

  errno = 0;
  pw = getpwnam(opt_user);
  if (!pw)
    {
      r = errno?-errno:-ENOENT;
      fprintf(stderr, "User `%s' does not exist.\n", opt_user);
      return r;
    }

  /* 'opt_user' points into argv[] (or the "root" literal), so it stays valid for the life of the process,
     unlike 'pw', which is backed by a static buffer getpwnam(3)/getpwuid(3) share and that build_environment()
     in ptybroker.c will clobber via its own getpwuid() call. */
  args.user = opt_user;

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
      args.chdir = chdir_home;
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

  return run_pty_session(&args, cmdline);
}

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

/* Emulates su(1)'s command line and defaults as closely as reasonably possible using PTYBroker.
   Authentication, environment handling and session setup are the broker's (and polkit's) job, what's
   left to do here is turning su's own command line into the equivalent PTYBroker request. su normally
   only forwards a small, curated environment (--preserve-environment / --whitelist-environment).
   su's -G/--supp-group has no equivalent: the PTYBroker only lets the shell backend run as a single,
   given primary group, not a list of supplementary ones. */
static int
main_su(int argc, char *argv[])
{
  _cleanup_free_ char *resolved_shell = NULL;
  _cleanup_free_ char *chdir_home = NULL;
  Exec0Args args = {};
  char *opt_user = "root";
  char *opt_shell = NULL;
  char *opt_command = NULL;
  bool opt_login = false;
  bool opt_fast = false;
  struct passwd *pw;
  unsigned n;
  int r;

  if (argc < 1)
    return -EINVAL;

  for (n = 1; n < (unsigned) argc; n++)
    {
      if (streq(argv[n], "--help"))
	{
	  help_su();
	  return 0;
	}
      else if (streq(argv[n], "--version"))
	{
	  printf("su (%s) %s\n", PACKAGE, VERSION);
	  return 0;
	}
      else if (streq(argv[n], "-") || streq(argv[n], "-l") || streq(argv[n], "--login"))
	opt_login = true;
      else if (streq(argv[n], "-f") || streq(argv[n], "--fast"))
	opt_fast = true;
      else if (streq(argv[n], "-m") || streq(argv[n], "-p") || streq(argv[n], "--preserve-environment"))
	args.preserve_environment = true;
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
	  args.group = argv[n];
	}
      else if (streq(argv[n], "-w") || streq(argv[n], "--whitelist-environment"))
	{
	  n++;
	  if (n >= (unsigned) argc)
	    {
	      help_su();
	      return -EINVAL;
	    }
	  args.whitelist_env = argv[n];
	}
      else if (streq(argv[n], "-P") || streq(argv[n], "--pty") ||
	       streq(argv[n], "-T") || streq(argv[n], "--no-pty"))
	/* exec0 always proxies a pty allocated by the broker; nothing to toggle. */
	;
      else if (streq(argv[n], "-v") || streq(argv[n], "--verbose"))
	args.verbose = true;
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
  args.user = opt_user;

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
      args.chdir = chdir_home;
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
     field in Exec0Args / "executable" in ptybroker.c), while the first element of 'commandLine' is only ever
     used as argv[0], so login-shell semantics are requested the same way su does it, by prefixing argv[0]
     with a dash. */
  args.executable = shell;

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

  return run_pty_session(&args, cmdline);
}

static void
help_exec0(void)
{
  printf("exec0 [OPTIONS...] [-- COMMAND...]\n\n"
	 "Open a shell (or COMMAND) through systemd-ptybrokerd's Varlink interface.\n\n"
	 "  -h, --help           Show this help\n"
	 "  -u, --user <name>    Run as the user \"name\"\n"
	 "  -g, --group <name>   Run as the group \"name\"\n"
	 "  -n, --name <name>    Register the PTY allocation under the given name\n"
	 "  -D, --chdir <patg>   Working directory for the shell (defaults to the current one)\n"
	 "  -v, --verbose        Show the acquired PTY's name and unit\n"
	 "  -V, --version        Show version\n");
}

static int
main_exec0(int argc, char *argv[])
{
  Exec0Args args = {};
  char **cmdline = NULL;

  static const struct option options[] = {
    { "help",    no_argument,       NULL, 'h' },
    { "group",   required_argument, NULL, 'g' },
    { "user",    required_argument, NULL, 'u' },
    { "name",    required_argument, NULL, 'n' },
    { "chdir",   required_argument, NULL, 'D' },
    { "verbose", no_argument,       NULL, 'v' },
    { "version", no_argument,       NULL, 'V' },
    {}
  };

  int c;
  while ((c = getopt_long(argc, argv, "hg:u:n:D:vV", options, NULL)) >= 0)
    switch (c)
      {
      case 'g':
	args.group = optarg;
	break;
      case 'u':
	args.user = optarg;
	break;
      case 'n':
	args.name = optarg;
	break;
      case 'D':
	args.chdir = optarg;
	break;
      case 'h':
	help_exec0();
	exit(EXIT_SUCCESS);
      case 'v':
	args.verbose = true;
	break;
      case 'V':
	printf("exec0 (%s) %s\n", PACKAGE, VERSION);
	return 0;
      default:
	fprintf(stderr, "Unexpected getopt result '%c'\n", c);
	return -EINVAL;
      }

  /* Anything left over after "--" is the command line to run on the backend side. If none is given
   * the broker invokes the caller's login shell. */
  cmdline = optind < argc ? argv + optind : NULL;

  return run_pty_session(&args, cmdline);
}

int
main (int argc, char **argv)
{
  int r;

  if (streq(basename(argv[0]), "pkexec"))
    r = main_pkexec(argc, argv);
  else if (streq(basename(argv[0]), "su"))
    r = main_su(argc, argv);
  else
    r = main_exec0(argc, argv);

  if (r < 0)
    return -r;

  return r;
}
