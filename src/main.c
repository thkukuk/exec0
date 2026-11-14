// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "basics.h"
#include "main.h"
#include "ptybroker.h"

// Looks up 'program' in $PATH, the way a shell (or execvp(3)) would.
char *
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

// Environment variables always start with a letter, can contain '_' and end with '='
bool
is_env_assignment(const char *arg)
{
  if (!(isalpha((unsigned char) arg[0]) || arg[0] == '_'))
    return false;

  const char *p = arg + 1;
  while (*p && *p != '=')
    {
      if (!isalnum((unsigned char) *p) && *p != '_')
        return false;
      p++;
    }

  return *p == '=';
}

// Checks whether 'arg' is a valid comma-separated list of environment variable names, as accepted
// by --preserve-env's optional argument (e.g. "PATH,HOME").
bool
is_env_list(const char *arg)
{
  if (isempty(arg))
    return false;

  const char *p = arg;
  while (*p)
    {
      if (!(isalpha((unsigned char) *p) || *p == '_'))
        return false;
      p++;

      while (*p && *p != ',')
        {
          if (!isalnum((unsigned char) *p) && *p != '_')
            return false;
          p++;
        }

      if (*p == ',')
        p++;
    }

  return true;
}

// Appends 'name' to the comma-separated list in '*list' (which may be NULL), growing it as needed.
int
whitelist_add(char **list, const char *name)
{
  char *tmp;

  if (*list)
    {
      if (asprintf(&tmp, "%s,%s", *list, name) < 0)
        return -ENOMEM;
    }
  else
    {
      tmp = strdup(name);
      if (!tmp)
        return -ENOMEM;
    }

  free(*list);
  *list = tmp;
  return 0;
}

/* Joins 'cfg' (a NULL-terminated argv tail) into a single string suitable for "shell -c STRING",
   quoting any shell metacharacters by backslash-escaping them. */
char *
quote_shell_command(char **cfg)
{
  size_t size = 1; /* trailing NUL */
  for (char **a = cfg; *a; a++)
    size += strlen(*a) * 2 + 1; /* worst case: every byte escaped, plus a separating space */

  char *cmd = malloc(size);
  if (!cmd)
    return NULL;

  char *dst = cmd;
  for (char **a = cfg; *a; a++)
    {
      for (const char *s = *a; *s; s++)
        {
          if (!isalnum((unsigned char) *s) && *s != '_' && *s != '-' && *s != '$')
            *dst++ = '\\';
          *dst++ = *s;
        }
      *dst++ = ' ';
    }
  if (dst != cmd)
    dst--; /* replace the trailing separator with the terminating NUL */
  *dst = '\0';

  return cmd;
}

static void
usage_exec0(FILE *fp)
{
  fprintf(fp,
	  "exec0 [-u user] [-g group] [-i | -s] [-E] [-D path] [VAR=value]... [COMMAND [ARGUMENT...]]\n"
          "exec0 -h | -v | -q | -V\n");
}

static void
help_exec0(void)
{
  usage_exec0(stdout);
  printf("\nOpen a shell (or COMMAND) through systemd-ptybrokerd.\n\n"
         "  -D, --chdir <directory>     Change the working directory before running command\n"
         "      --description <text>    Set a description for the pty session\n"
         "  -E, --preserve-env          Preserve user environment when running command\n"
         "      --preserve-env <list>   Preserve specific, comma-separated environment variables\n"
         "  -u, --user <user>           Run command as the specified user (default: root)\n"
         "  -g, --group <group>         Run command as the specified group\n"
         "  -h, --help                  Display this help message and exit\n"
         "  -i, --login                 Run a login shell as the target user\n"
         "      --name <name>           Set the name of the pty session\n"
         "  -s, --shell                 Run a shell as the target user\n"
	 "      --background <color>    Set ANSI color for background\n"
	 "  -q, --quiet                 Be quit, disables verbose settings\n"
         "  -v, --verbose               Print verbose information\n"
         "  -V, --version               Show version and exit\n");
}

static int
main_exec0(int argc, char *argv[])
{
  _cleanup_free_ char *argv0 = NULL;
  _cleanup_free_ char *quoted_cmd = NULL;
  _cleanup_free_ char *whitelist = NULL;
  config_t cfg = {};
  bool opt_login = false;
  bool opt_shell = false;
  char *opt_preserve_list = NULL;

  int n;
  int r;
  econf_err cfg_err;

  if (argc < 1)
    return -EINVAL;

  /* Load /etc/exec0/exec0.conf (or /usr/share/exec0/exec0.conf) first, so its settings act as
     defaults that the command line parsed below can still override. */
  cfg_err = read_config("exec0", &cfg);
  if (cfg_err != ECONF_SUCCESS && cfg_err != ECONF_NOFILE)
    fprintf(stderr, "exec0: failed to read configuration: %s\n", econf_errString(cfg_err));

  for (n = 1; n < argc; n++)
    {
      char *arg = argv[n];

      if (streq(arg, "--"))
        {
          n++;
          break;
        }
      else if (streq(arg, "-h") || streq(arg, "--help"))
        {
          help_exec0();
          return 0;
        }
      else if (streq(arg, "-V") || streq(arg, "--version"))
        {
          printf("exec0 (%s) %s\n", PACKAGE, VERSION);
          return 0;
        }
      else if (streq(arg, "-u") || streq(arg, "--user"))
        {
          if (++n >= argc)
            {
              fprintf(stderr, "exec0: option requires an argument -- 'u'\n");
              usage_exec0(stderr);
              return -EINVAL;
            }
          cfg.user = argv[n];
        }
      else if (strneq(arg, "--user=", 7))
        cfg.user = arg + 7;
      else if (streq(arg, "-g") || streq(arg, "--group"))
        {
          if (++n >= argc)
            {
              fprintf(stderr, "exec0: option requires an argument -- 'g'\n");
              usage_exec0(stderr);
              return -EINVAL;
            }
          cfg.group = argv[n];
        }
      else if (strneq(arg, "--group=", 8))
        cfg.group = arg + 8;
      else if (streq(arg, "-D") || streq(arg, "--chdir"))
        {
          if (++n >= argc)
            {
              fprintf(stderr, "exec0: option requires an argument -- 'D'\n");
              usage_exec0(stderr);
              return -EINVAL;
            }
          cfg.chdir = argv[n];
        }
      else if (strneq(arg, "--chdir=", 8))
        cfg.chdir = arg + 8;
      else if (streq(arg, "-i") || streq(arg, "--login"))
        opt_login = true;
      else if (streq(arg, "-s") || streq(arg, "--shell"))
        opt_shell = true;
      else if (streq(arg, "-E"))
        cfg.preserve_environment = true;
      else if (streq(arg, "--preserve-env"))
        {
          /* Optional argument: take the next word as the list only if it actually looks like
             one, i.e. contains a comma (bare "--preserve-env" means "preserve everything", same
             as -E, and must not swallow a single-word COMMAND, e.g. "--preserve-env bash"). A
             single variable name still works via "--preserve-env=NAME". */
          if (n + 1 < argc && strchr(argv[n + 1], ',') && is_env_list(argv[n + 1]))
            opt_preserve_list = argv[++n];
          else
            cfg.preserve_environment = true;
        }
      else if (strneq(arg, "--preserve-env=", 15))
        opt_preserve_list = arg + 15;
      else if (streq(arg, "-v") || streq(arg, "--verbose"))
        cfg.verbose = true;
      else if (streq(arg, "-q") || streq(arg, "--quiet"))
        cfg.verbose = false;
      else if (streq(arg, "--name"))
        {
          if (++n >= argc)
            {
              fprintf(stderr, "exec0: option requires an argument -- 'name'\n");
              usage_exec0(stderr);
              return -EINVAL;
            }
          cfg.name = argv[n];
        }
      else if (streq(arg, "--description"))
        {
          if (++n >= argc)
            {
              fprintf(stderr, "exec0: option requires an argument -- 'description'\n");
              usage_exec0(stderr);
              return -EINVAL;
            }
          cfg.description = argv[n];
        }
      else if (arg[0] == '-' && arg[1] != '\0')
        {
          fprintf(stderr, "exec0: invalid option -- '%s'\n", arg + (arg[1] == '-' ? 2 : 1));
          usage_exec0(stderr);
          return -EINVAL;
        }
      else
        break; /* first non-option argument: a VAR=value assignment, or the command */
    }

  if (opt_login && opt_shell)
    {
      fprintf(stderr, "exec0: you may not specify both the -i and -s options\n");
      usage_exec0(stderr);
      return -EINVAL;
    }

  if (opt_login && cfg.preserve_environment)
    {
      fprintf(stderr, "exec0: you may not specify both the -i and -E options\n");
      usage_exec0(stderr);
      return -EINVAL;
    }

  if (opt_preserve_list && opt_preserve_list[0] == '\0')
    {
      fprintf(stderr, "exec0: --preserve-env requires a non-empty list\n");
      usage_exec0(stderr);
      return -EINVAL;
    }

  if (opt_preserve_list && !cfg.preserve_environment)
    {
      whitelist = strdup(opt_preserve_list);
      if (!whitelist)
        {
          fprintf(stderr, "Out of memory!\n");
          return -ENOMEM;
        }
    }

  /* Anything left that looks like VAR=value is an environment variable assignment for the command,
     same as sudo(1): set it in our own environment (so getpwnam()/build_environment() downstream,
     and -E's "preserve everything" below, pick it up) and, unless the whole environment is already
     being preserved, whitelist it explicitly so build_environment() forwards it. */
  while (n < argc && is_env_assignment(argv[n]))
    {
      char *eq = strchr(argv[n], '=');

      *eq = '\0';
      r = setenv(argv[n], eq + 1, 1);
      if (r == 0 && !cfg.preserve_environment)
        r = whitelist_add(&whitelist, argv[n]);
      *eq = '=';
      if (r < 0)
        {
          fprintf(stderr, "Out of memory!\n");
          return -ENOMEM;
        }

      n++;
    }

  bool have_cmd = n < argc;

  errno = 0;
  struct passwd *pw = getpwnam(isempty(cfg.user)?"root":cfg.user);
  if (!pw)
    {
      r = errno ? -errno : -ENOENT;
      fprintf(stderr, "User %s does not exist (%s)\n", cfg.user, strerror(-r));
      return r;
    }

  if (cfg.preserve_environment)
    cfg.preserve_environment = true;
  else if (whitelist)
    cfg.whitelist_env = whitelist;

  if (opt_login && !cfg.chdir)
    cfg.chdir = pw->pw_dir;

  /* Plain "exec0" with nothing else given runs a shell, same as "-s": go through the same
     shell-building code path below so it gets a non-login shell, rather than leaving 'executable'
     and 'commandLine' both unset, which asks the broker for its own default shell semantics (a
     login shell). */
  if (opt_login || opt_shell || !have_cmd)
    {
      char *shell;

      if (opt_shell)
        {
          char *envshell = getenv("SHELL");
          shell = !isempty(envshell)?envshell:
	    (!isempty(pw->pw_shell)?"/bin/sh":pw->pw_shell);
        }
      else // opt_login or no command
        shell = isempty(pw->pw_shell)?"/bin/sh":pw->pw_shell;

      if (access(shell, X_OK) != 0)
        {
          r = -errno;
          fprintf(stderr, "Error accessing %s: %s\n", shell, strerror(-r));
          return r;
        }

      cfg.executable = shell;

      /* the broker executes 'executable' directly, while the first element of 'commandLine' is
	 used as argv[0], so login-shell semantics is prefixing argv[0] with a dash. */
      if (opt_login)
        r = asprintf(&argv0, "-%s", basename(shell));
      else
        {
          if ((argv0 = strdup(basename(shell))) == NULL)
	    r = -1;
	  else
	    r = 0;
        }
      if (r < 0)
        {
          fprintf(stderr, "Out of memory!\n");
          return -ENOMEM;
        }

      size_t n_cmdline = 1 + (have_cmd ? 2 : 0) + 1;
      _cleanup_free_ char **built = calloc(n_cmdline, sizeof(char *));
      if (!built)
        {
          fprintf(stderr, "Out of memory!\n");
          return -ENOMEM;
        }

      size_t i = 0;
      built[i++] = argv0;
      if (have_cmd)
        {
          quoted_cmd = quote_shell_command(argv + n);
          if (!quoted_cmd)
            {
              fprintf(stderr, "Out of memory!\n");
              return -ENOMEM;
            }
          built[i++] = (char *) "-c";
          built[i++] = quoted_cmd;
        }
      built[i] = NULL;

      return run_pty_session(&cfg, built);
    }

  // Plain "exec0 COMMAND [ARGUMENT...]": run it directly
  if (argv[n][0] != '/')
    {
      _cleanup_free_ char *resolved_path = find_program_in_path(argv[n]);
      if (!resolved_path)
        {
          fprintf(stderr, "Command not found: %s\n", argv[n]);
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

int
main (int argc, char **argv)
{
  int r;

  if (streq(basename(argv[0]), "pkexec"))
    r = main_pkexec(argc, argv);
  else if (streq(basename(argv[0]), "su"))
    r = main_su(argc, argv);
  else if (streq(basename(argv[0]), "sudo"))
    r = main_sudo(argc, argv);
  else
    r = main_exec0(argc, argv);

  if (r < 0)
    return -r;

  return r;
}
