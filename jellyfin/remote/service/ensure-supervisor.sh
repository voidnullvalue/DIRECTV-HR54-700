#!/bin/sh
# cronie launches this at boot and every minute. flock remains held by runsv,
# so only one supervisor can run; runsv itself restarts the Python backend.
#
# ============================ HISTORICAL ============================
# Development-host supervision for the SUPERSEDED Python backend. The
# production appliance does not use runsv; the receiver-native backend
# hr54-jf is started directly by the persistent /var boot hook.
# See docs/PERSISTENCE.md.
# ====================================================================
set -eu

: "${HR54_REPO:?set HR54_REPO to the checkout root}"

STATE="${HR54_STATE:-${XDG_STATE_HOME:-$HOME/.local/state}/hr54-jellyfin}"
RUN="${HR54_RUN:-${TMPDIR:-/tmp}/hr54-remote}"

mkdir -p "$STATE" "$RUN"
exec /usr/bin/flock -n "$RUN/hr54-runsv.lock" \
    /usr/bin/runsv "$HR54_REPO/jellyfin/remote/service" \
    >> "$STATE/supervisor.log" 2>&1
