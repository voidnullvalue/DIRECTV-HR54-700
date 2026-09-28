#!/bin/sh
umask 022

# The first action is deliberately a shell builtin plus redirection to the
# persistent XFS filesystem.  It has no dependency on /tmp or on any external
# firmware utility.
echo HR54_ROOT_EXEC > /var/HR54_ROOT_PROOF
sync
exit 0
