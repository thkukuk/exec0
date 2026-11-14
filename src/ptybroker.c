// SPDX-License-Identifier: GPL-2.0-or-later

/* Everything related to talking to systemd-ptybrokerd's Varlink interface
   (io.systemd.PTYBroker. */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <termios.h>
#include <unistd.h>

#include <systemd/sd-json.h>
#include <systemd/sd-varlink.h>

#include "basics.h"
#include "polkit.h"
#include "ptybroker.h"
#include "unitwatch.h"


#define VARLINK_SOCKET "/run/systemd/io.systemd.PTYBroker"

/* Builds a TerminalSettings object describing our own terminal, so the
   broker can match the allocated PTY to it as closely as possible.
   Fields that are unknown or not meaningful are simply left out, in
   xwhich case the broker falls back to its own defaults. */
static int
terminal_settings_to_json(sd_json_variant **ret)
{
  const char *term = getenv("TERM");
  const char *colorterm = getenv("COLORTERM");
  const char *no_color = getenv("NO_COLOR");

  struct winsize ws = {};
  bool have_winsize = isatty(STDIN_FILENO) && ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) >= 0 &&
    ws.ws_col > 0 && ws.ws_row > 0;

  return sd_json_buildo(ret,
                        SD_JSON_BUILD_PAIR_CONDITION(!!term, "envTERM", SD_JSON_BUILD_STRING(term)),
                        SD_JSON_BUILD_PAIR_CONDITION(!!colorterm, "envCOLORTERM", SD_JSON_BUILD_STRING(colorterm)),
                        SD_JSON_BUILD_PAIR_CONDITION(no_color && no_color[0], "envNO_COLOR", SD_JSON_BUILD_BOOLEAN(true)),
                        SD_JSON_BUILD_PAIR_CONDITION(have_winsize, "columns", SD_JSON_BUILD_UNSIGNED(ws.ws_col)),
                        SD_JSON_BUILD_PAIR_CONDITION(have_winsize, "lines", SD_JSON_BUILD_UNSIGNED(ws.ws_row)));
}

/* Builds the WorkingDirectory object for AcquirePty(): either the path given via --chdir, or, like run0,
   the directory we were invoked from (rather than the target user's home directory). */
static int
working_directory_to_json(const config_t *cfg, sd_json_variant **ret)
{
  _cleanup_free_ char *cwd = NULL;
  const char *path = cfg->chdir;

  if (!path)
    {
      cwd = getcwd(NULL, 0);
      if (!cwd)
	return -errno;
      path = cwd;
    }

  return sd_json_buildo(ret,
                        SD_JSON_BUILD_PAIR_STRING("path", path),
                        SD_JSON_BUILD_PAIR_BOOLEAN("missingOK", false));
}

/* Appends a "VAR=VALUE" string (ownership taken on success, via TAKE_PTR) to a NULL-terminated,
   realloc()ed string array, growing it by one slot. On failure *kv is left untouched, so a
   _cleanup_free_ caller still frees it. */
static int
env_add(char ***envp, size_t *n, char **kv)
{
  if (!*kv)
    return -ENOMEM;

  char **tmp = realloc(*envp, (*n + 2) * sizeof(char *));
  if (!tmp)
    return -ENOMEM;

  tmp[*n] = TAKE_PTR(*kv);
  tmp[*n + 1] = NULL;
  *envp = tmp;
  (*n)++;
  return 0;
}

static void
strv_free(char **v)
{
  if (!v)
    return;
  for (char **p = v; *p; p++)
    free(*p);
  free(v);
}

static void
strv_freep(char ***v)
{
  strv_free(*v);
}

/* Returns true if the first 'len' bytes of 's' form a valid environment variable name (the same rule
   execve(2)/getenv(3) implicitly assume: a leading letter or underscore, followed by letters, digits
   or underscores). Bash exports shell functions as environment entries named "BASH_FUNC_<name>%%",
   which fail this (on account of the '%'s) and must be filtered out of a preserved environment before
   it is handed to the broker: unlike a shell, which only ever iterates its own environment array
   without validating names, systemd-ptybrokerd itself rejects the whole AcquirePty() call outright
   (SD_VARLINK_ERROR_INVALID_PARAMETER) if asked to set even a single invalid one. */
static bool
env_name_valid(const char *s, size_t len)
{
  if (len == 0 || (!isalpha((unsigned char) s[0]) && s[0] != '_'))
    return false;

  for (size_t i = 1; i < len; i++)
    if (!isalnum((unsigned char) s[i]) && s[i] != '_')
      return false;

  return true;
}

// Returns true if 'env' (an array of "VAR=VALUE" strings, 'n' of them) already has an entry for 'name'.
static bool
env_has(char **env, size_t n, const char *name)
{
  size_t len = strlen(name);

  for (size_t i = 0; i < n; i++)
    if (strneq(env[i], name, len) && env[i][len] == '=')
      return true;

  return false;
}

/* Builds the environment to hand to the shell on the backend side. By
   default this is a small, curated set of variables forwarded from the
   own environment if set, plus anything named by cfg->whitelist_env
   (su(1)'s -w/--whitelist-environment); if cfg->preserve_environment
   is set instead (su(1)'s -p/--preserve-environment), the whole environment
   is forwarded verbatim. In both cases this is topped off with
   SHELL_PROMPT_PREFIX defaulting to a fixed marker if the caller didn't
   already customize it, and SUDO_USER/SUDO_UID/SUDO_GID identifying the
   real, invoking user, for compatibility with scripts that only know how
   to look for sudo's conventions, unless those are already present (which,
   with cfg->preserve_environment, they may well be).

   Returns 0 and sets *ret_env on success, a negative errno on failure. */
static int
build_environment(const config_t *cfg, char ***ret_env)
{
  static const char *const pass_through[] = { "SHELL", "XDG_SESSION_CLASS" };

  _cleanup_(strv_freep) char **env = NULL;
  size_t n = 0;

  if (cfg->preserve_environment)
    {
      for (char **e = environ; e && *e; e++)
	{
	  const char *eq = strchr(*e, '=');
	  if (!eq || !env_name_valid(*e, (size_t) (eq - *e)))
	    continue;

	  _cleanup_free_ char *kv = strdup(*e);
	  if (!kv)
	    return -ENOMEM;

	  int r = env_add(&env, &n, &kv);
	  if (r < 0)
	    return r;
	}
    }
  else
    {
      for (size_t i = 0; i < sizeof pass_through / sizeof pass_through[0]; i++)
	{
	  const char *value = getenv(pass_through[i]);
	  if (!value)
	    continue;

	  _cleanup_free_ char *kv = NULL;
	  if (asprintf(&kv, "%s=%s", pass_through[i], value) < 0)
	    return -ENOMEM;

	  int r = env_add(&env, &n, &kv);
	  if (r < 0)
	    return r;
	}

      if (cfg->whitelist_env)
	{
	  _cleanup_free_ char *copy = strdup(cfg->whitelist_env);
	  if (!copy)
	    return -ENOMEM;

	  char *saveptr = NULL;
	  for (char *name = strtok_r(copy, ",", &saveptr); name; name = strtok_r(NULL, ",", &saveptr))
	    {
	      const char *value = getenv(name);
	      if (!value)
		continue;

	      _cleanup_free_ char *kv = NULL;
	      if (asprintf(&kv, "%s=%s", name, value) < 0)
		return -ENOMEM;

	      int r = env_add(&env, &n, &kv);
	      if (r < 0)
		return r;
	    }
	}
    }

  errno = 0;
  struct passwd *pw = getpwuid(getuid());
  // XXX error check

  /* Mark the shell as privileged in the prompt, like run0 does: forward the caller's own prefix if they
     already customized it, otherwise fall back to a default one. */
  if (!cfg->preserve_environment || !env_has(env, n, "SHELL_PROMPT_PREFIX"))
    {
      const char *prompt_prefix = getenv("SHELL_PROMPT_PREFIX");
      _cleanup_free_ char *kv = NULL;

      if (asprintf(&kv, "SHELL_PROMPT_PREFIX=%s", prompt_prefix ?: "\U0001F9B8 ") < 0)
	return -ENOMEM;

      int r = env_add(&env, &n, &kv);
      if (r < 0)
	return r;
    }

  if (pw && (!cfg->preserve_environment || !env_has(env, n, "SUDO_USER")))
    {
      _cleanup_free_ char *kv = NULL;

      if (asprintf(&kv, "SUDO_USER=%s", pw->pw_name) < 0)
	return -ENOMEM;

      int r = env_add(&env, &n, &kv);
      if (r < 0)
	return r;
    }

  if (!cfg->preserve_environment || !env_has(env, n, "SUDO_UID"))
    {
      _cleanup_free_ char *kv = NULL;

      if (asprintf(&kv, "SUDO_UID=%u", (unsigned) getuid()) < 0)
	return -ENOMEM;

      int r = env_add(&env, &n, &kv);
      if (r < 0)
	return r;
    }

  if (!cfg->preserve_environment || !env_has(env, n, "SUDO_GID"))
    {
      _cleanup_free_ char *kv = NULL;

      if (asprintf(&kv, "SUDO_GID=%u", (unsigned) getgid()) < 0)
	return -ENOMEM;

      int r = env_add(&env, &n, &kv);
      if (r < 0)
	return r;
    }

  *ret_env = TAKE_PTR(env);
  return 0;
}

/* Connects to the broker and calls AcquirePty(), asking it to run a shell (or the given command line) on
   the backend side, and to hand us a monitor connection for the frontend side. AcquirePty() is a Varlink
   method that "supports upgrade": on success the connection stops being a regular Varlink connection and
   turns into a plain bidirectional byte stream (handed back to us as two dup()ed file descriptors) that
   carries the PTY's output to us and our input back to it. */
static int
acquire_shell_pty(const config_t *cfg, char **cmdline,
		  int *ret_monitor_fd, char **ret_unit)
{
  int r;

  _cleanup_(sd_varlink_unrefp) sd_varlink *vl = NULL;
  r = sd_varlink_connect_address(&vl, VARLINK_SOCKET);
  if (r < 0)
    {
      fprintf(stderr, "Failed to connect to %s: %s\n",
	      VARLINK_SOCKET, strerror(-r));
      return r;
    }

  /* The reply to AcquirePty() carries file descriptors (the monitor connection), hence this must be
     enabled explicitly. */
  r = sd_varlink_set_allow_fd_passing_input(vl, true);
  if (r < 0)
    {
      fprintf(stderr, "Failed to enable file descriptor passing: %s\n", strerror(-r));
      return r;
    }

  _cleanup_(sd_json_variant_unrefp) sd_json_variant *terminal_settings = NULL;
  r = terminal_settings_to_json(&terminal_settings);
  if (r < 0)
    {
      fprintf(stderr, "Failed to build terminal settings: %s\n", strerror(-r));
      return r;
    }

  _cleanup_(sd_json_variant_unrefp) sd_json_variant *working_directory = NULL;
  r = working_directory_to_json(cfg, &working_directory);
  if (r < 0)
    {
      fprintf(stderr, "Failed to determine working directory: %s\n", strerror(-r));
      return r;
    }

  _cleanup_(strv_freep) char **environment = NULL;
  r = build_environment(cfg, &environment);
  if (r < 0)
    {
      fprintf(stderr, "Failed to build environment: %s\n", strerror(-r));
      return r;
    }

  bool have_cmdline = cmdline && cmdline[0];

  /* Best-effort: try to get a polkit authentication agent running on our terminal before making the
     call, so that if the broker's polkit check ends up requiring a password, there is actually
     someone around who can ask for and answer it. Whether this succeeds or not, we still tell the
     broker that interactive authentication is fine by us (see allowInteractiveAuthentication below);
     if no agent could be started, polkit itself will simply be unable to prompt and the whole call
     will fail (though not with SD_VARLINK_ERROR_INTERACTIVE_AUTHENTICATION_REQUIRED, see below). */
  __attribute__((cleanup(polkit_agent_closep))) pid_t agent_pid = polkit_agent_open();

  int input_fd = -1, output_fd = -1;
  sd_json_variant *reply = NULL; /* borrowed from vl, do not unref */
  const char *error_id = NULL;
  r = sd_varlink_call_and_upgradebo(vl,
				    "io.systemd.PTYBroker.AcquirePty",
				    &reply,
				    &error_id,
				    &input_fd,
				    &output_fd,
				    SD_JSON_BUILD_PAIR_STRING("frontendType", "null"),
				    SD_JSON_BUILD_PAIR_STRING("backendType", "shell"),
				    SD_JSON_BUILD_PAIR_BOOLEAN("monitor", true),
				    SD_JSON_BUILD_PAIR_BOOLEAN("osc2811", true),
				    SD_JSON_BUILD_PAIR_CONDITION(!!cfg->name, "name", SD_JSON_BUILD_STRING(cfg->name)),
				    SD_JSON_BUILD_PAIR_CONDITION(!!cfg->description, "description", SD_JSON_BUILD_STRING(cfg->description)),
				    SD_JSON_BUILD_PAIR_CONDITION(!!cfg->user, "user", SD_JSON_BUILD_STRING(cfg->user)),
				    SD_JSON_BUILD_PAIR_CONDITION(!!cfg->group, "group", SD_JSON_BUILD_STRING(cfg->group)),
				    SD_JSON_BUILD_PAIR_CONDITION(have_cmdline, "commandLine", SD_JSON_BUILD_STRV(cmdline)),
				    SD_JSON_BUILD_PAIR_CONDITION(have_cmdline, "viaShell", SD_JSON_BUILD_BOOLEAN(false)),
				    SD_JSON_BUILD_PAIR_CONDITION(!!cfg->executable, "executable", SD_JSON_BUILD_STRING(cfg->executable)),
				    SD_JSON_BUILD_PAIR_VARIANT("terminalSettings", terminal_settings),
				    SD_JSON_BUILD_PAIR_VARIANT("workingDirectory", working_directory),
				    SD_JSON_BUILD_PAIR_CONDITION(!!environment, "environment", SD_JSON_BUILD_STRV(environment)),
				    SD_JSON_BUILD_PAIR_BOOLEAN("allowInteractiveAuthentication", true));

  if (r < 0)
    {
      fprintf(stderr, "Failed to call AcquirePty(): %s\n", strerror(-r));
      return r;
    }
  if (error_id)
    {
      /* This error means the broker's polkit check required authentication, but *this very
	 call* did not allow interactive authentication for it (allowInteractiveAuthentication
	 was false, or absent) -- it is not a statement about whether a polkit agent happens to be
	 running. We do pass allowInteractiveAuthentication: true above, so seeing this would mean
	 the broker ignored that; still, keep a correct, actionable message instead of blaming a
	 missing agent. */
      if (strcmp(error_id, SD_VARLINK_ERROR_INTERACTIVE_AUTHENTICATION_REQUIRED) == 0)
	fprintf(stderr,
		"Failed to acquire PTY from ptybrokerd: this operation requires "
		"authentication, and this client did not allow interactive authentication "
		"for it.\n");
      else
	fprintf(stderr, "Failed to acquire PTY from ptybrokerd: %s\n", error_id);

      if (input_fd >= 0)
	close(input_fd);
      if (output_fd >= 0)
	close(output_fd);
      return -EIO;
    }

  /* The two returned file descriptors are dup()s of the very same upgraded socket (one conceptually
     for "input", one for "output"); we only need one of them to talk to it. */
  if (input_fd < 0)
    {
      fprintf(stderr, "Did not receive a monitor file descriptor from the broker.\n");
      if (output_fd >= 0)
	close(output_fd);
      return -EPROTO;
    }
  if (output_fd >= 0)
    close(output_fd);

  sd_json_variant *unit = sd_json_variant_by_key(reply, "unit");

  if (cfg->verbose)
    {
      sd_json_variant *name = sd_json_variant_by_key(reply, "name");
      fprintf(stderr, "Connected to PTY '%s'%s%s.\n",
	      name ? sd_json_variant_string(name) : "?",
	      unit ? ", unit " : "",
	      unit ? sd_json_variant_string(unit) : "");
    }

  if (ret_unit)
    /* The transient unit systemd-ptybrokerd started the shell as, e.g. "shell-foo.service": not
       carried any further by the PTYBroker protocol itself (AcquirePty() carries no information
       at all about how the backend process eventually exits), but the caller needs it to watch
       the unit directly via the service manager's D-Bus API, the same way run0 does. */
    *ret_unit = unit ? strdup(sd_json_variant_string(unit)) : NULL;

  *ret_monitor_fd = input_fd;

  return 0;
}

static struct termios saved_termios;
static bool have_saved_termios = false;

static void
restore_terminal(void)
{
  if (have_saved_termios)
    tcsetattr(STDIN_FILENO, TCSADRAIN, &saved_termios);
}

static int
set_raw_terminal(void)
{
  if (!isatty(STDIN_FILENO))
    return 0;

  if (tcgetattr(STDIN_FILENO, &saved_termios) < 0)
    return -errno;

  have_saved_termios = true;
  atexit(restore_terminal);

  struct termios raw = saved_termios;
  cfmakeraw(&raw);
  if (tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) < 0)
    return -errno;

  return 0;
}

/* Implements the client side of UAPI spec 17 ("OSC 2811: Terminal Size Change Notification"), as used on
   the monitor stream when osc2811 was requested at AcquirePty() time: the broker periodically embeds a
   "subscribe" sequence (ESC ] 2811 ; ? ; columns=N ; lines=M ST) into the session output it sends us,
   declaring the dimensions it currently believes are in effect; we reply, only if they actually differ
   from our terminal's real dimensions, with a "report" sequence (ESC ] 2811 ; columns=N ; lines=M ST) on
   the same connection, which the broker then applies to the backend pty. A "cancel" (the same request
   marker, but without any fields) means the broker is no longer interested until the next subscribe. None
   of these sequences are meant for our own terminal and must not reach it; anything else (including a
   reflected report, which must be ignored and passed through as-is per spec) is left untouched. */

enum {
  OSC_GROUND,    /* plain text */
  OSC_ESC,       /* saw ESC, waiting to see whether this is an OSC introducer ("]") */
  OSC_BODY,      /* inside an OSC body, collecting until a terminator (BEL, or ESC "\") */
  OSC_BODY_ESC,  /* inside an OSC body, saw ESC, waiting to see whether it completes "ESC \" (ST) */
  CSI_SEQUENCE,  /* inside a CSI sequence (ESC "["), collecting parameter/intermediate bytes */
};

#define OSC_BODY_MAX 256
#define CSI_BODY_MAX 192

typedef struct {
  int state;
  char body[OSC_BODY_MAX];
  size_t body_len;

  /* A pending, unanswered subscription from the broker, and the dimensions it declared. */
  bool subscribed;
  unsigned columns, lines;

  /* Whether unrecognized OSC sequences (e.g. logind's OSC 3008 session-tracking marker, emitted into
     the backend's own output by pam_systemd regardless of whether anyone is watching for it) should be
     passed through verbatim. A real terminal swallows those invisibly, so this is set from
     isatty(STDOUT_FILENO); piped into anything else, they would otherwise show up as literal garbage
     in the output. */
  bool pass_through_unknown;

  /* Background color tinting (--background / the "background" config setting): if set, the ANSI SGR
     parameter string (e.g. "44" or "48;5;142") to tint the terminal background with. 'tint_background' is
     precomputed once (also factoring in whether tinting is actually possible/wanted, see
     run_pty_session()), so the hot path below only ever needs to check this one flag. */
  const char *background_color;
  bool tint_background;
  char csi_buf[CSI_BODY_MAX];
  size_t csi_len;
} Osc2811Scanner;

/* Parses the payload of an OSC sequence (the bytes between the introducer and the terminator, NUL
   terminated, not including either). Returns 0 if this is not an OSC 2811 subscribe/cancel request (which
   includes the case where it is a 2811 *report*, reflected back at us: as per spec that must be ignored,
   i.e. passed through), or the parsed request otherwise. */
static int
osc2811_parse(const char *seq, bool *ret_cancel, unsigned *ret_columns, unsigned *ret_lines)
{
  if (strncmp(seq, "2811", 4) != 0)
    return 0;
  const char *p = seq + 4;
  if (*p != ';' && *p != '\0')
    return 0; /* some other, unrelated OSC number that merely shares the "2811" prefix */

  if (!(p[0] == ';' && p[1] == '?' && (p[2] == ';' || p[2] == '\0')))
    return 0; /* no request marker: a report, reflected back at us; ignore/pass through */
  p += 2;

  unsigned columns = 0, lines = 0;
  bool any_fields = false;
  while (p[0] == ';')
    {
      p++;
      size_t n = strcspn(p, ";");
      if (strncmp(p, "columns=", 8) == 0)
	columns = (unsigned) strtoul(p + 8, NULL, 10);
      else if (strncmp(p, "lines=", 6) == 0)
	lines = (unsigned) strtoul(p + 6, NULL, 10);
      /* unknown fields are ignored, as per spec */
      any_fields = true;
      p += n;
    }

  *ret_cancel = !any_fields;
  *ret_columns = columns;
  *ret_lines = lines;
  return 1;
}

/* If a subscription is currently pending and our terminal's real dimensions differ from what the broker
   declared, sends it a report and marks the subscription consumed (the broker is expected to immediately
   re-subscribe, declaring the corrected dimensions back at us). Harmless no-op otherwise, including when
   stdin isn't a terminal or its size can't be determined. */
static int
osc2811_maybe_report(Osc2811Scanner *osc, int monitor_fd)
{
  if (!osc->subscribed)
    return 0;

  struct winsize ws;
  if (!isatty(STDIN_FILENO) || ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0 || ws.ws_row == 0)
    return 0;

  if (ws.ws_col == osc->columns && ws.ws_row == osc->lines)
    return 0;

  char seq[64];
  int n = snprintf(seq, sizeof seq, "\x1b]2811;columns=%u;lines=%u\x1b\\", ws.ws_col, ws.ws_row);
  if (n < 0 || (size_t) n >= sizeof seq)
    return -EINVAL;

  ssize_t off = 0;
  while (off < n)
    {
      ssize_t w = write(monitor_fd, seq + off, n - off);
      if (w < 0)
	{
	  if (errno == EINTR)
	    continue;
	  return -errno;
	}
      off += w;
    }

  osc->subscribed = false;
  return 1;
}

/* NUL-terminates and parses the just-completed OSC body. If it's a recognized 2811 request, acts on it
   (updating the subscription state and replying if appropriate) and reports it as consumed via *ret_excise;
   otherwise (unrecognized, or a reflected report), *ret_excise is set to the opposite of
   osc->pass_through_unknown: flushed verbatim for a real terminal to (invisibly) deal with, excised
   otherwise. */
static int
osc2811_handle_body(Osc2811Scanner *osc, int monitor_fd, bool *ret_excise)
{
  bool cancel;
  unsigned columns, lines;

  osc->body[osc->body_len] = 0;

  if (osc2811_parse(osc->body, &cancel, &columns, &lines) <= 0)
    {
      *ret_excise = !osc->pass_through_unknown;
      return 0;
    }

  if (cancel)
    osc->subscribed = false;
  else
    {
      osc->subscribed = true;
      osc->columns = columns;
      osc->lines = lines;

      int r = osc2811_maybe_report(osc, monitor_fd);
      if (r < 0)
	return r;
    }

  *ret_excise = true;
  return 0;
}

/* Appends an SGR sequence that (re-)applies the background color to 'out', and returns the number of bytes
   written. Only ever called with osc->tint_background set, i.e. osc->background_color is non-NULL. */
static size_t
append_background_tint(const Osc2811Scanner *osc, char *out)
{
  size_t len = 0;

  out[len++] = '\x1b';
  out[len++] = '[';
  size_t l = strlen(osc->background_color);
  memcpy(out + len, osc->background_color, l);
  len += l;
  out[len++] = 'm';

  return len;
}

/* Parses the parameter string of a CSI "m" (SGR, "Select Graphic Rendition") sequence, e.g. "0" or
   "38;5;142", and determines whether it resets the background back to the terminal's default (as opposed to
   leaving it alone, or setting it to something else). Used to decide whether our own background color needs
   to be patched back in right after it. */
static bool
is_csi_background_reset_sequence(const char *seq)
{
  enum { TOKEN_NONE, TOKEN_START, TOKEN_8BIT, TOKEN_24BIT_R, TOKEN_24BIT_G, TOKEN_24BIT_B } token_state = TOKEN_NONE;
  bool reset = false;

  for (;;)
    {
      size_t l = strcspn(seq, ";");
      char token[16];

      if (l >= sizeof token)
	l = sizeof token - 1;
      memcpy(token, seq, l);
      token[l] = '\0';

      switch (token_state)
	{
	case TOKEN_NONE:
	  if (streq(token, "") || streq(token, "0") || streq(token, "00") || streq(token, "49"))
	    reset = true; /* these tokens reset the background back to normal */
	  else if (streq(token, "40") || streq(token, "41") || streq(token, "42") || streq(token, "43") ||
		   streq(token, "44") || streq(token, "45") || streq(token, "46") || streq(token, "47") ||
		   streq(token, "48"))
	    reset = false; /* and these set it to something other than normal */

	  if (streq(token, "38") || streq(token, "48") || streq(token, "58"))
	    token_state = TOKEN_START; /* an 8bit or 24bit color spec follows */
	  break;

	case TOKEN_START:
	  if (streq(token, "5") || streq(token, "05"))
	    token_state = TOKEN_8BIT;
	  else if (streq(token, "2") || streq(token, "02"))
	    token_state = TOKEN_24BIT_R;
	  else
	    token_state = TOKEN_NONE;
	  break;

	case TOKEN_24BIT_R:
	  token_state = TOKEN_24BIT_G;
	  break;
	case TOKEN_24BIT_G:
	  token_state = TOKEN_24BIT_B;
	  break;

	case TOKEN_8BIT:
	case TOKEN_24BIT_B:
	  token_state = TOKEN_NONE;
	  break;
	}

      seq += l;
      if (*seq == '\0')
	break;
      seq++; /* skip ';' */
    }

  return reset;
}

/* Scans 'n' freshly read bytes from the monitor connection, writing everything that isn't a recognized OSC
   2811 request to 'out' (which must have room for at least n + OSC_BODY_MAX + 16 bytes, or, if background
   tinting is enabled, n * (BACKGROUND_COLOR_MAX_LEN + 4) + OSC_BODY_MAX + CSI_BODY_MAX bytes: the worst case
   is a buffer of nothing but newlines, each of which may grow into a full SGR sequence setting the
   background color back), and acting on, and excising, any request found; also patches background-tinting
   SGR sequences into the stream when osc->tint_background is set (see run_pty_session()). Returns the number
   of bytes written to 'out', or <0 on error. */
static ssize_t
osc2811_scan(Osc2811Scanner *osc, int monitor_fd, const char *in, size_t n, char *out)
{
  size_t out_len = 0;

  for (size_t i = 0; i < n; i++)
    {
      char c = in[i];

      switch (osc->state)
	{
	case OSC_GROUND:
	  if (c == '\x1b')
	    osc->state = OSC_ESC;
	  else
	    {
	      out[out_len++] = c;
	      /* Right after a newline or carriage return, re-assert the background color: terminals
		 typically paint freshly scrolled-in lines with their own default background, not
		 whatever was last set via SGR. */
	      if ((c == '\n' || c == '\r') && osc->tint_background)
		out_len += append_background_tint(osc, out + out_len);
	    }
	  break;
	case OSC_ESC:
	  if (c == ']')
	    {
	      osc->state = OSC_BODY;
	      osc->body_len = 0;
	    }
	  else if (c == '[' && osc->background_color)
	    {
	      osc->state = CSI_SEQUENCE;
	      osc->csi_len = 0;
	    }
	  else
	    {
	      out[out_len++] = '\x1b';
	      out[out_len++] = c;
	      osc->state = OSC_GROUND;
	      /* "ESC c" is a full terminal reset ("RIS"), which also clears the background color. */
	      if (c == 'c' && osc->tint_background)
		out_len += append_background_tint(osc, out + out_len);
	    }
	  break;
	case CSI_SEQUENCE:
	  if (c >= 0x20 && c <= 0x3f)
	    {
	      if (osc->csi_len >= sizeof osc->csi_buf - 1)
		{
		  /* Way longer than any legitimate CSI sequence: give up, flush verbatim. */
		  out[out_len++] = '\x1b';
		  out[out_len++] = '[';
		  memcpy(out + out_len, osc->csi_buf, osc->csi_len);
		  out_len += osc->csi_len;
		  out[out_len++] = c;
		  osc->state = OSC_GROUND;
		}
	      else
		osc->csi_buf[osc->csi_len++] = c;
	    }
	  else
	    {
	      /* The CSI sequence is over: 'c' is its final byte. Flush it verbatim, except for an SGR
		 ("m") sequence that resets the background, where the fix is patched directly into it
		 (so e.g. a colored prompt followed by a plain reset keeps a single escape sequence), and
		 a "CSI ! p" soft reset ("DECSTR"), which, like the full reset above, also needs a fresh
		 background color sequence appended after it. */
	      osc->csi_buf[osc->csi_len] = '\0';

	      out[out_len++] = '\x1b';
	      out[out_len++] = '[';
	      memcpy(out + out_len, osc->csi_buf, osc->csi_len);
	      out_len += osc->csi_len;

	      if (c == 'm' && osc->tint_background && is_csi_background_reset_sequence(osc->csi_buf))
		{
		  out[out_len++] = ';';
		  size_t l = strlen(osc->background_color);
		  memcpy(out + out_len, osc->background_color, l);
		  out_len += l;
		}

	      out[out_len++] = c;

	      if (c == 'p' && osc->tint_background && streq(osc->csi_buf, "!"))
		out_len += append_background_tint(osc, out + out_len);

	      osc->state = OSC_GROUND;
	    }
	  break;
	case OSC_BODY_ESC:
	  if (c != '\\')
	    {
	      /* Not a valid ST after all: give up on this as an OSC sequence, flush
		 everything seen so far verbatim, and reprocess this byte from scratch. */
	      out[out_len++] = '\x1b';
	      out[out_len++] = ']';
	      memcpy(out + out_len, osc->body, osc->body_len);
	      out_len += osc->body_len;
	      osc->state = OSC_GROUND;
	      i--;
	      continue;
	    }
	  else
	    {
	      bool excise;
	      int r = osc2811_handle_body(osc, monitor_fd, &excise);
	      if (r < 0)
		return r;
	      if (!excise)
		{
		  out[out_len++] = '\x1b';
		  out[out_len++] = ']';
		  memcpy(out + out_len, osc->body, osc->body_len);
		  out_len += osc->body_len;
		  out[out_len++] = '\x1b';
		  out[out_len++] = '\\';
		}
	      osc->state = OSC_GROUND;
	    }
	  break;
	case OSC_BODY:
	  if (c == '\x1b')
	    {
	      osc->state = OSC_BODY_ESC;
	      break;
	    }
	  if (c == '\a')
	    {
	      bool excise;
	      int r = osc2811_handle_body(osc, monitor_fd, &excise);
	      if (r < 0)
		return r;
	      if (!excise)
		{
		  out[out_len++] = '\x1b';
		out[out_len++] = ']';
		memcpy(out + out_len, osc->body, osc->body_len);
                                        out_len += osc->body_len;
                                        out[out_len++] = '\a';
                                }
                                osc->state = OSC_GROUND;
                        } else if (osc->body_len < sizeof osc->body - 1)
                                osc->body[osc->body_len++] = c;
                        else {
                                /* Way longer than any legitimate OSC 2811 sequence: give up, flush verbatim. */
                                out[out_len++] = '\x1b';
                                out[out_len++] = ']';
                                memcpy(out + out_len, osc->body, osc->body_len);
                                out_len += osc->body_len;
                                out[out_len++] = c;
                                osc->state = OSC_GROUND;
                        }
                        break;
                }
        }

        return (ssize_t) out_len;
}

static int
make_sigwinch_fd(void)
{
  sigset_t mask;

  sigemptyset(&mask);
  sigaddset(&mask, SIGWINCH);

  if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
    return -errno;

  int fd = signalfd(-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
  if (fd < 0)
    return -errno;

  return fd;
}

/* Whether background tinting (--background, or the "background" setting from the configuration file)
   should actually be applied: only makes sense when our output is going to a terminal, and honours
   $NO_COLOR, the same convention terminal_settings_to_json() already reports to the broker via
   envNO_COLOR. */
static bool
shall_tint_background(const config_t *cfg)
{
  const char *no_color;

  if (isempty(cfg->background_color))
    return false;

  if (!isatty(STDOUT_FILENO))
    return false;

  no_color = getenv("NO_COLOR");
  if (no_color && no_color[0])
    return false;

  return true;
}

/* Copies bytes between our own stdin/stdout and the monitor connection, until the monitor connection is
   closed (i.e. the session ends) *and* (if 'watch' is given and actually watching anything) the backend
   unit has reported its final exit status: the PTY closing and the unit's D-Bus state settling are two
   independent, racy notifications of the same underlying event (the backend process exiting), and the exit
   status is only available from the latter (see unitwatch.h). Also filters OSC 2811 terminal-size-change
   traffic out of the monitor connection and replies to it (see osc2811_scan()), reports SIGWINCH-driven
   resizes of our own terminal the same way, and, if cfg->background_color is set and shall_tint_background()
   agrees, tints the terminal background with it for the duration of the session. */
static int
forward_pty(const config_t *cfg, int monitor_fd, int sigwinch_fd, UnitWatch *watch)
{
  struct pollfd pollfds[4] = {
    { .fd = STDIN_FILENO, .events = POLLIN },
    { .fd = monitor_fd,   .events = POLLIN },
    { .fd = sigwinch_fd,  .events = POLLIN },
    { .fd = watch ? unit_watch_get_fd(watch) : -1, .events = POLLIN },
  };

  bool tint_background = shall_tint_background(cfg);
  Osc2811Scanner osc = {
    .pass_through_unknown = isatty(STDOUT_FILENO),
    .background_color = cfg->background_color,
    .tint_background = tint_background,
  };
  bool pty_done = false;

  if (tint_background)
    {
      /* Set the initial background color, and erase to the end of the (first) line with it, same as
	 systemd's run0 does. */
      char seq[8 + BACKGROUND_COLOR_MAX_LEN + 4];
      int len = snprintf(seq, sizeof seq, "\x1b[%sm\x1b[K", cfg->background_color);

      if (len > 0 && (size_t) len < sizeof seq)
	{
	  ssize_t off = 0;
	  while (off < len)
	    {
	      ssize_t w = write(STDOUT_FILENO, seq + off, (size_t) len - off);
	      if (w < 0)
		{
		  if (errno == EINTR)
		    continue;
		  break; /* best-effort: a failure here shouldn't abort the whole session */
		}
	      off += w;
	    }
	}
    }

  for (;;)
    {
      /* Once the PTY side is done, there is nothing left to forward; all that's left is a short
	 grace period for the unit watch (if any) to catch up and report the final exit status. If
	 it doesn't show up in time (lost signal, bus hiccup, ...) give up and let the caller report
	 a best-effort status from whatever was last seen, rather than hanging forever. */
      int timeout = pty_done ? 2000 : -1;

      /* poll(2) ignores entries with a negative fd (used once stdin is half-closed below, if
       * SIGWINCH tracking could not be set up at all, and if there is no unit watch to wait for). */
      int r = poll(pollfds, 4, timeout);
      if (r < 0)
	{
	  if (errno == EINTR)
	    continue;
	  return -errno;
	}
      if (r == 0 && pty_done)
	return 0; /* grace period for the unit watch elapsed; report best effort */

      if (pollfds[3].fd >= 0 && (pollfds[3].revents & (POLLIN|POLLHUP|POLLERR)))
	{
	  int ur = unit_watch_process(watch);
	  if (ur < 0)
	    /* The bus connection broke; no further updates will ever arrive, so stop
	       waiting on it and report whatever was last seen. */
	    watch->done = true;
	}

      if (pollfds[2].fd >= 0 && (pollfds[2].revents & POLLIN))
	{
	  struct signalfd_siginfo si;
	  while (read(pollfds[2].fd, &si, sizeof si) == sizeof si)
	    ; /* drain; a single fresh TIOCGWINSZ below covers any number of signals */

	  r = osc2811_maybe_report(&osc, monitor_fd);
	  if (r < 0)
	    fprintf(stderr, "Warning: failed to report terminal size change: %s\n", strerror(-r));
	}

      if (pollfds[0].fd >= 0 && (pollfds[0].revents & (POLLIN|POLLHUP|POLLERR)))
	{
	  char buf[4096];
	  ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
	  if (n < 0 && errno != EINTR && errno != EAGAIN)
	    return -errno;
	  if (n == 0)
	    /* Our input went away (e.g. stdin is not a terminal but a pipe that ran
	       dry). Stop polling it, but keep reading the session's output until the
	       monitor connection itself is closed: the broker's monitor protocol has no
	       concept of a half-closed input, so we must not shut down or close
	       monitor_fd here. On a real interactive terminal this never triggers, since
	       a raw-mode tty never yields a read() of 0. */
	    pollfds[0].fd = -1;
	  if (n > 0)
	    {
	      ssize_t off = 0;
	      while (off < n)
		{
		  ssize_t w = write(monitor_fd, buf + off, n - off);
		  if (w < 0)
		    {
		      if (errno == EINTR)
			continue;
		      return -errno;
		    }
		  off += w;
		}
	    }
	}

      if (pollfds[1].fd >= 0 && (pollfds[1].revents & (POLLIN|POLLHUP|POLLERR)))
	{
	  char buf[4096];
	  ssize_t n = read(monitor_fd, buf, sizeof buf);
	  if (n < 0 && errno != EINTR && errno != EAGAIN)
	    return -errno;
	  if (n == 0)
	    {
	      /* Session ended: stop polling the monitor connection, but don't return yet,
		 see the comment on forward_pty() above. */
	      pty_done = true;
	      pollfds[1].fd = -1;
	    }
	  if (n > 0)
	    {
	      /* See osc2811_scan()'s doc comment for why this needs to be this generous when tinting is
		 enabled: worst case, every single byte read is a newline, each of which can grow into a
		 full SGR sequence re-asserting the background color. */
	      char filtered[sizeof buf * (BACKGROUND_COLOR_MAX_LEN + 4) + OSC_BODY_MAX + CSI_BODY_MAX];
	      ssize_t flen = osc2811_scan(&osc, monitor_fd, buf, (size_t) n, filtered);
	      if (flen < 0)
		return (int) flen;

	      ssize_t off = 0;
	      while (off < flen)
		{
		  ssize_t w = write(STDOUT_FILENO, filtered + off, flen - off);
		  if (w < 0)
		    {
		      if (errno == EINTR)
			continue;
		      return -errno;
		    }
		  off += w;
		}
	    }
	}

      if (pty_done && (!watch || !watch->have_ref || watch->done))
	return 0;
    }
}

/* Connects to the broker, forwards the terminal to the acquired PTY until the session ends, and restores
   the terminal settings afterwards.

   On success, the return value is the actual exit status (0-255) of the backend shell or command, the same
   way a shell reports $? for a foreground job.
   A negative return is for failures of the function itself. */
int
run_pty_session(const config_t *cfg, char **cmdline)
{
  _cleanup_close_ int monitor_fd = -EBADF;
  _cleanup_free_ char *unit = NULL;
  int r;

  r = acquire_shell_pty(cfg, cmdline, &monitor_fd, &unit);
  if (r < 0)
    return r;

  _cleanup_(unit_watch_done) UnitWatch watch = {};
  if (unit)
    {
      r = unit_watch_start(unit, &watch);
      if (r < 0)
        fprintf(stderr, "Warning: failed to watch unit '%s', exit status will not be available: %s\n",
                unit, strerror(-r));
    }

  signal(SIGPIPE, SIG_IGN);

  r = set_raw_terminal();
  if (r < 0)
    fprintf(stderr, "Warning: failed to switch terminal to raw mode: %s\n",
            strerror(-r));

  _cleanup_close_ int sigwinch_fd = -EBADF;
  if (isatty(STDIN_FILENO))
    {
      sigwinch_fd = make_sigwinch_fd();
      if (sigwinch_fd < 0)
        {
          fprintf(stderr,
                  "Warning: failed to watch for terminal size changes: %s\n",
                  strerror(-sigwinch_fd));
          sigwinch_fd = -EBADF;
        }
    }

  r = forward_pty(cfg, monitor_fd, sigwinch_fd, &watch);

  restore_terminal();


  if (r < 0)
    {
      fprintf(stderr, "PTY forwarding failed: %s\n", strerror(-r));
      return r;
    }

  return unit_watch_exit_status(&watch);
}
