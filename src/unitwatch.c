// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "basics.h"
#include "unitwatch.h"

#define DESTINATION "org.freedesktop.systemd1"
#define UNIT_INTERFACE "org.freedesktop.systemd1.Unit"

/* Applies one "sv" dict entry (the message cursor must be positioned right after reading the key string) to
   'w', updating w->done once ActiveState (which every unit has) settles. Unknown keys, and any key whose
   variant doesn't have the expected contents type, are simply skipped: this dict is the union of every
   interface the unit object implements (see the empty-interface GetAll() call in refresh() below), so most
   of it is of no interest to us. */
static void
apply_property(UnitWatch *w, const char *key, sd_bus_message *m)
{
  if (streq(key, "ActiveState")) {
    const char *s;
    if (sd_bus_message_read(m, "v", "s", &s) >= 0) {
      free(w->active_state);
      w->active_state = strdup(s);
    }
  } else if (streq(key, "Result")) {
    const char *s;
    if (sd_bus_message_read(m, "v", "s", &s) >= 0) {
      free(w->result);
      w->result = strdup(s);
    }
  } else if (streq(key, "ExecMainCode")) {
    int32_t v;
    if (sd_bus_message_read(m, "v", "i", &v) >= 0)
      w->exec_main_code = v;
  } else if (streq(key, "ExecMainStatus")) {
    int32_t v;
    if (sd_bus_message_read(m, "v", "i", &v) >= 0)
      w->exec_main_status = v;
  } else
    sd_bus_message_skip(m, "v");

  if (w->active_state && (streq(w->active_state, "inactive") || streq(w->active_state, "failed")))
    w->done = true;
}

/* Parses the "a{sv}" property dictionary returned by Properties.GetAll(), applying every entry found to 'w'.
   The message cursor must be positioned right before the array. */
static int
parse_property_dict(UnitWatch *w, sd_bus_message *m)
{
  int r;

  r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
  if (r < 0)
    return r;

  for (;;) {
    const char *key;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
    if (r < 0)
      return r;
    if (r == 0)
      break;

    r = sd_bus_message_read(m, "s", &key);
    if (r < 0)
      return r;

    apply_property(w, key, m);

    r = sd_bus_message_exit_container(m);
    if (r < 0)
      return r;
  }

  return sd_bus_message_exit_container(m);
}

/* Synchronously re-fetches the complete, current state of the unit (see the "" interface trick in
   unit_watch_start()) and applies it to 'w'. Used both for the initial snapshot and, crucially, every time a
   PropertiesChanged signal fires: ActiveState (on the Unit interface) and Result/ExecMainCode/ExecMainStatus
   (on the Service interface) are updated by two separate property-change notifications, with no guaranteed
   order between them, so a signal's own payload is not enough to tell whether w->done is actually warranted
   yet. Re-reading everything in one GetAll() sidesteps that ordering race entirely: it always reflects
   whatever is true of the unit *right now*, by which point a transition that has already happened is fully
   settled. This mirrors run_context_update() in systemd's own src/run/run.c. */
static int
refresh(UnitWatch *w)
{
  _cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
  _cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
  int r;

  r = sd_bus_call_method(w->bus, DESTINATION, w->path,
                          "org.freedesktop.DBus.Properties", "GetAll",
                          &error, &reply, "s", "");
  if (r < 0)
    return r;

  return parse_property_dict(w, reply);
}

/* org.freedesktop.DBus.Properties.PropertiesChanged(s interface, a{sv} changed, as invalidated): the
   payload itself is deliberately ignored in favour of refresh(), see its comment above. */
static int
on_properties_changed(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
  UnitWatch *w = userdata;

  (void) m;
  (void) error;

  (void) refresh(w); /* best-effort: a failure here just means we keep the last known state */
  return 0;
}

int
unit_watch_start(const char *unit, UnitWatch *ret)
{
  _cleanup_free_ char *path = NULL;
  int r;

  *ret = (UnitWatch) {};

  r = sd_bus_path_encode("/org/freedesktop/systemd1/unit", unit, &path);
  if (r < 0)
    return r;

  sd_bus *bus = NULL;
  r = sd_bus_open_system(&bus);
  if (r < 0) {
    /* No bus to talk to: not fatal, we just won't be able to report a real exit status. */
    return 0;
  }

  /* TODO(unit-ref-by-broker): this Unit.Ref() D-Bus call is exactly the pattern
   * https://github.com/systemd/systemd/pull/44357 ("Followups for ptybrokerd/run0", open/unmerged as of
   * 2026-11-06) removes from run0, for a real reason that applies to us identically: calling Unit.Ref()
   * ourselves, as an otherwise-unprivileged client, requires a *second*, separate polkit authorization
   * (action "org.freedesktop.systemd1.manage-units", checked in bus_unit_method_ref() in systemd's
   * src/core/dbus-unit.c) on top of whatever ptybrokerd's own AcquirePty() polkit check already required.
   * In practice the two often don't visibly double-prompt, because polkit's "auth_admin_keep" grace period
   * (~5 minutes) from the first authorization frequently still covers the second -- but that's incidental,
   * not guaranteed, and the upstream commit message for that PR ("run: ask ptybrokerd to keep transient
   * unit referenced") spells out that double-prompting is exactly the bug it fixes.
   *
   * That PR adds a "referenceUnit" boolean input and a "unitReferenceFileDescriptor" output to
   * AcquirePty() itself (see the SD_VARLINK_DEFINE_INPUT/OUTPUT additions in
   * src/shared/varlink-io.systemd.PTYBroker.c): when requested, ptybrokerd -- which is already privileged
   * and already did its own authorization for AcquirePty() -- opens an io.systemd.Unit.AcquireReference()
   * streaming call to the service manager *itself* (a new Varlink method, see
   * src/shared/varlink-io.systemd.Unit.c), and hands the client a plain file descriptor as a token: as
   * long as it stays open, the unit stays referenced (pinned against CollectMode=inactive_or_failed
   * garbage collection, same purpose Ref() serves here), and closing it releases the reference. No
   * additional client-side authorization is needed at all, because the client never talks to the service
   * manager's privileged interface directly for this.
   *
   * Once that PR has merged and shipped in a released libsystemd this project can require (meson.build
   * currently pins 'libsystemd', version : '>= 257'; check what version first ships AcquireReference /
   * PTYBroker's referenceUnit and bump accordingly), the fix here is mechanical and confined to this
   * function and acquire_shell_pty() in ptybroker.c:
   *   1. In acquire_shell_pty()'s AcquirePty() call, add SD_JSON_BUILD_PAIR_BOOLEAN("referenceUnit", true).
   *   2. Parse the new "unitReferenceFileDescriptor" output field from the reply (an index into the
   *      Varlink call's received file descriptors, the same way "unit" is parsed today) and take it via
   *      sd_varlink_take_fd(), passing it through to unit_watch_start() (or folding acquire_shell_pty()'s
   *      unit-handling into this file) instead of just the unit name.
   *   3. Delete the sd_bus_call_method(..., "Ref", ...) call below entirely; store the taken fd in
   *      UnitWatch instead (e.g. w->reference_fd) and treat it as a second thing forward_pty() polls for
   *      alongside the PropertiesChanged bus fd, watching for EPOLLERR/EPOLLHUP (meaning ptybrokerd itself
   *      lost the reference, e.g. because the service manager restarted badly -- see
   *      on_unit_reference_lost() in run0's src/run/run.c for the equivalent handling) and closing it only
   *      after unit_watch_exit_status() has read the final result.
   * Everything else in this file -- the PropertiesChanged match, the GetAll("") refresh() trick, exit
   * status derivation -- is untouched by that PR and needs no changes.
   */
  _cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
  r = sd_bus_call_method(bus, DESTINATION, path, UNIT_INTERFACE, "Ref", &error, NULL, NULL);
  if (r < 0) {
    /* The unit may already be gone (it was very short-lived), or this may require authentication we don't
       have: either way, give up gracefully rather than failing the whole session over it. */
    sd_bus_unref(bus);
    return 0;
  }

  ret->bus = bus;
  ret->path = TAKE_PTR(path);
  ret->have_ref = true;

  r = sd_bus_match_signal(ret->bus, &ret->slot,
                           DESTINATION, ret->path,
                           "org.freedesktop.DBus.Properties", "PropertiesChanged",
                           on_properties_changed, ret);
  if (r < 0)
    return r;

  /* Pick up the current state right away: the unit may already have finished in the window between
     AcquirePty() returning and Ref() completing above, in which case no further PropertiesChanged signal
     will ever arrive. */
  (void) refresh(ret); /* best-effort: we'll still get PropertiesChanged signals going forward */
  return 0;
}

void
unit_watch_done(UnitWatch *w)
{
  if (!w)
    return;

  w->slot = sd_bus_slot_unref(w->slot);
  w->bus = sd_bus_unref(w->bus);
  w->path = mfree(w->path);
  w->active_state = mfree(w->active_state);
  w->result = mfree(w->result);
}

int
unit_watch_get_fd(const UnitWatch *w)
{
  if (!w->have_ref)
    return -1;

  return sd_bus_get_fd(w->bus);
}

int
unit_watch_process(UnitWatch *w)
{
  int r, n = 0;

  if (!w->have_ref)
    return 0;

  for (;;) {
    r = sd_bus_process(w->bus, NULL);
    if (r < 0)
      return r;
    if (r == 0)
      return n;
    n++;
  }
}

int
unit_watch_exit_status(const UnitWatch *w)
{
  if (!w->have_ref || !w->result)
    return 0;

  if (streq(w->result, "success"))
    return 0;

  if (streq(w->result, "exit-code"))
    return w->exec_main_status > 0 ? w->exec_main_status : EXIT_FAILURE;

  if (w->exec_main_code == CLD_KILLED || w->exec_main_code == CLD_DUMPED)
    return 128 + w->exec_main_status;

  return EXIT_FAILURE;
}
