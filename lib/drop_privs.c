// SPDX-License-Identifier: LGPL-2.1-or-later

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "drop_privs.h"

int
drop_privs(void)
{
  /* drop gid */
  if (setgid(getgid()) != 0)
    return -errno;

  /* drop uid */
  if (setuid(getuid()) != 0)
    return -errno;

  /* Try to regain root. If this succeeds, we failed to drop
     privileges. Don't do this as root, else the check will fail. */
  if (getuid() != 0 && setuid(0) != -1)
    return -EPERM;

  return 0;
}
