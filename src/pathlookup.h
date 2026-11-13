// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* Looks up 'program' in $PATH, the way a shell (or execvp(3)) would. Returns a newly allocated,
   absolute path on success, or NULL if not found (or on allocation failure). Shared by the various
   compat wrappers (su, pkexec, sudo) to resolve a bare shell/command name before handing it to the
   broker, which (unlike execvp(3)) does not search $PATH itself. */
char *find_program_in_path(const char *program);
