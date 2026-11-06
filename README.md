# exec0

This project provides `exec0`, a binary executing shells or commands
as root or another user via `systemd-ptybrokerd.service`. Authentication
is done via polkit. No setuid binaries are needed, so that this tool
works with `NoNewPrivs` set.


## polkit rules

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

## wrappers

Additional, this package provides wrappers for pkexec, su and in the
future sudo.
