#!/system/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective
# OpenABL Installer job @NAME@. The app writes it and a root shell runs it.
JOB=@JOB@
WORK=@WORK@
STATUS=@STATUS@
COPY=$WORK/@NAME@.sh

# Run from /data/local/tmp, in the background, so that the root tool returns
# at once and nothing keeps a file open on shared storage.
if [ "$0" != "$COPY" ]; then
  mkdir -p "$WORK" && cp "$0" "$COPY" && { nohup sh "$COPY" > /dev/null 2>&1 & }
  exit 0
fi
XF=$WORK/@NAME@.extra
trap 'rm -f "$COPY" "$XF" "$STATUS.tmp"' EXIT
: > "$XF"

own() { set -- $(ls -ldn "$1" 2>/dev/null); echo "$3:$4"; }
OWNER=$(own "$(dirname "$STATUS")")

# Status file: state, message, then whatever put added. The app owns it.
say() {
  { printf 'job=%s\nstate=%s\nmessage=%s\n' "$JOB" "$1" "$2"; cat "$XF"; } > "$STATUS.tmp" || return
  [ -z "$OWNER" ] || chown "$OWNER" "$STATUS.tmp" 2> /dev/null
  chmod 660 "$STATUS.tmp"
  mv -f "$STATUS.tmp" "$STATUS"
}
put() { echo "$1=$2" >> "$XF"; }
fail() { say failed "$1"; exit 1; }

say started "Starting"
