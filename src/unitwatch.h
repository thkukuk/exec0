// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

#include <systemd/sd-bus.h>

/* Tracks the exit status of the transient "shell-*.service" unit systemd-ptybrokerd starts for us, the same
   way run0 does it (see start_via_ptybroker() in systemd's src/run/run.c): io.systemd.PTYBroker.AcquirePty()
   itself carries no information whatsoever about how the backend process eventually exits, so this watches
   the unit directly via the service manager's D-Bus API, in parallel to the PTY forwarding. */
typedef struct {
  sd_bus *bus;
  sd_bus_slot *slot;
  char *path;

  /* True once Unit.Ref() succeeded, i.e. once we actually have something to wait for. If this stays false
     (no bus, no permission, or the unit was already gone by the time we got around to it) the caller has no
     choice but to fall back to reporting plain success, same as run0 does in that case. */
  bool have_ref;

  /* True once the unit's ActiveState has settled on "inactive" or "failed", i.e. the main process has
     exited and its exit information below is final. */
  bool done;

  char *active_state;
  char *result;          /* Service's "Result" property, e.g. "success", "exit-code", "signal", ... */
  int exec_main_code;     /* CLD_EXITED, CLD_KILLED, CLD_DUMPED, ... (0 if not seen yet) */
  int exec_main_status;   /* exit code, or signal number, depending on exec_main_code */
} UnitWatch;

/* Starts watching 'unit' (a transient unit name as returned by AcquirePty() in its "unit" output field).
   Returns 0 on success -- including the case where watching could not be set up, in which case
   ret->have_ref stays false and the caller should just treat the session as a plain success once it ends --
   or a negative errno if 'unit' itself couldn't even be turned into a valid bus path (out of memory), which
   is the only situation worth treating as an outright failure here. */
int unit_watch_start(const char *unit, UnitWatch *ret);

/* Releases all resources held by 'w'. Idempotent, and fine to call on an all-zero UnitWatch
   (e.g. unit_watch_start() was never called because AcquirePty() returned no "unit"). */
void unit_watch_done(UnitWatch *w);

/* The file descriptor to poll() for POLLIN alongside the PTY forwarding, or -1 if there is nothing to
   watch (w->have_ref is false). */
int unit_watch_get_fd(const UnitWatch *w);

/* Drains and processes all currently pending bus traffic. Returns the number of messages processed (which
   may be 0), or a negative errno if the bus connection broke; the latter is not fatal to the caller, it just
   means w->done should be assumed from here on, since no further updates will ever arrive. */
int unit_watch_process(UnitWatch *w);

/* Translates the unit's final Result/ExecMainCode/ExecMainStatus into a process exit status, using the same
   conventions as waitpid(2)/$?: a clean exit keeps its exit code, death by signal becomes 128+signal. If
   w->have_ref is false (nothing could be watched) this returns 0, i.e. "assume success" -- the same
   fallback run0 uses. */
int unit_watch_exit_status(const UnitWatch *w);
