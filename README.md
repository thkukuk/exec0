# exec0

This project provides `exec0`, a binary executing shells or commands
as root or another user via `systemd-ptybrokerd.service`. Authentication
is done via polkit. No setuid binaries are needed, so that this tool
works with `NoNewPrivs` set.


## polit rules

With the provided polkit rules:

* Users in the \_exec0\_nopasswd list don't need to enter a password.
* Users that are in the wheel group always authenticate with their
  own password.
* The remaining users need to provide the root password.

This matches `sudo-policy-auth-wheel-self` configuration on openSUSE
Tumbleweed, MicroOS and SLES 16.

To specify a list of users who don't need a password, create
a file `/etc/polkit-1/rules.d/51-exec0-nopasswd.rules` with the
following example content:

``` 
polkit._exec0_nopasswd.push("user1","user2",...);
```

## Wrappers

Additional, this package provides wrappers for pkexec, su and sudo. These
re-implement the respective command line on top of `exec0`/PtyBroker, they
do not call the real `su`/`sudo`/`pkexec`. Because authentication and
authorization are always delegated to polkit on every invocation, anything
related to a timestamp/ticket cache, PAM stacks or sudoers-style per-command
policy has no equivalent and is either a no-op or rejected outright.

### su

Supports the common options: `-c`/`--command`/`--session-command`, `-f`,
`-g`/`--group`, `-`/`-l`/`--login`, `-m`/`-p`/`--preserve-environment`,
`-s`/`--shell` and `-w`/`--whitelist-environment`.

* `-P`/`--pty` and `-T`/`--no-pty` are accepted but are dummies: a
  pseudo-terminal is always allocated via `systemd-ptybrokerd`.
* `-G`/`--supp-group` is rejected: supplementary groups cannot be set via
  the PtyBroker.
* Like real `su`, the current directory is kept unless `--login` is given,
  which switches to the target user's home.

### sudo

Supports `-D`/`--chdir`, `-E`/`--preserve-env[=list]`, `-g`/`--group`,
`-H`/`--set-home`, `-i`/`--login`, `-s`/`--shell`, `-u`/`--user` and leading
`VAR=value` environment assignments.

* `-k`/`--reset-timestamp`, `-K`/`--remove-timestamp`, `-v`/`--validate` and
  `-n`/`--non-interactive` are accepted for compatibility but are dummies,
  since there is no timestamp cache and polkit always authenticates
  interactively.
* `-e`/`--edit` (sudoedit), `-l`/`--list` and `-b`/`--background` have no
  sensible equivalent via the PtyBroker and are rejected.
* There is no sudoers file; who may run what as whom is governed entirely
  by the polkit rules, not by per-command/per-host policy.

### pkexec

Supports `-u`/`--user` and `-v`/`--verbose`.

* `--disable-internal-agent` is accepted but is a dummy: the internal
  polkit agent is already a best-effort, per-process helper that cannot be
  turned off separately.
* As with real `pkexec`, the working directory is switched to the target
  user's home unless `--keep-cwd` is given.
