// SPDX-License-Identifier: GPL-2.0-or-later

#include <errno.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>

#include "basics.h"
#include "polkit.h"

extern char **environ;

/* Spawns a text-mode polkit authentication agent (pkttyagent) for the
   duration of a privileged call, so that a polkit rule requiring a password
   can actually prompt for one on our terminal. This mirrors systemd's own
   polkit_agent_open() (src/shared/polkit-agent.c) and its callers in e.g.
   src/ptybroker/ptyctl.c: it is a best-effort step taken *before* the call,
   independent of whether the call ends up actually needing authentication.

   pkttyagent is given no --process/--system-bus-name, so it registers itself
   for its parent process, i.e. this very process, which is exactly the
   subject the broker's CheckAuthorization call builds from the peer's
   credentials. Readiness is signalled by pkttyagent closing the notify-fd
   once it has registered with polkitd; if it exits instead (not installed,
   no polkit, registration failed, ...), there is simply no usable agent and
   we proceed without one.

   Returns the agent's pid on success, or -errno in error case. */
pid_t
polkit_agent_open(void)
{
  _cleanup_free_ char *fd_str = NULL;
  posix_spawn_file_actions_t file_actions;
  int pipe_fd[2];
  pid_t pid;
  int r;

  if (geteuid() == 0 || !isatty(STDIN_FILENO))
    return -1;

  if (pipe(pipe_fd) < 0)
    return -errno;

  if (asprintf(&fd_str, "%d", pipe_fd[1]) < 0)
    return -ENOMEM;

  char *argv[] = {"pkttyagent", "--notify-fd", fd_str, "--fallback", NULL };

  r = posix_spawn_file_actions_init(&file_actions);
  if (r != 0)
    {
      close(pipe_fd[0]);
      close(pipe_fd[1]);
      return -r;
    }

  /* The child has no use for the read end; only the write end
     (passed via --notify-fd) is inherited. */
  posix_spawn_file_actions_addclose(&file_actions, pipe_fd[0]);

  r = posix_spawnp(&pid, "pkttyagent", &file_actions, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&file_actions);
  close(pipe_fd[1]);

  if (r != 0)
    {
      close(pipe_fd[0]);
      return -r;
    }

  struct pollfd pfd = { .fd = pipe_fd[0], .events = POLLIN };
  int pr = poll(&pfd, 1, 5000);
  close(pipe_fd[0]);

  int status;
  if (pr <= 0 || waitpid(pid, &status, WNOHANG) == pid)
    {
      /* timeout, poll error, or the child already exited: no usable agent */
      kill(pid, SIGTERM);
      waitpid(pid, NULL, 0);
      return -ESRCH; // XXX really correct?
    }

  return pid;
}

void
polkit_agent_closep(pid_t *pid)
{
  if (*pid <= 0)
    return;

  kill(*pid, SIGTERM);
  waitpid(*pid, NULL, 0);
}
