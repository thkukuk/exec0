// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <unistd.h>

extern pid_t polkit_agent_open(void);
extern void polkit_agent_closep(pid_t *pid);
