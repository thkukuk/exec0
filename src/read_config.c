// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include <pwd.h>
#include <ctype.h>
#include <errno.h>
#include <libeconf.h>

#include "basics.h"
#include "main.h"

/* XXX
config_t *
struct_config_free(config_t *cfg)
{
  // nothing to do yet, maybe later
  return NULL;
}
*/

static econf_err
get_bool_value(econf_file *key_file, const char *group, const char *key, bool *result)
{
  // XXX Use getStringValue and check for true/false/on/off/enabled/disabled/...
  econf_err error = econf_getBoolValue(key_file, group, key, result);

  if (error == ECONF_SUCCESS || error == ECONF_NOFILE ||
      error == ECONF_NOGROUP || error == ECONF_NOKEY)
    return ECONF_SUCCESS;

  return error;
}


/* we will read everything and only report the first error */
econf_err
read_config(const char *name, config_t *cfg)
{
  _cleanup_(econf_freeFilep) econf_file *key_file = NULL;
  econf_err error;

  /* This looks for metrics-exporter.conf{.d} in /usr/share/metrics-exporter/
     or /etc/account-utils/ */
  error = econf_readConfig(&key_file,
			   PACKAGE,            /* project name */
			   "/usr/share",       /* directory below /usr */
			   PACKAGE,            /* file name without extension */
			   "conf",             /* suffix */
			   "=",                /* delimiter */
			   "#");               /* comment */
  if (error != ECONF_SUCCESS)
    return error;

  error = get_bool_value(key_file, "[Global]", "verbose", &(cfg->verbose));
  if (error)
    return error;

  error = get_bool_value(key_file, name, "verbose", &(cfg->verbose));
  if (error)
    return error;

  return ECONF_SUCCESS;
}
