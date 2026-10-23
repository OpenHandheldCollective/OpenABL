#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective
#
# Runs `make test` in a Debian container with the tools it checks against:
# Docker if it's running, else Apple's container. The images are written
# inside the container; the largest is a sparse 98 GiB.

set -eu
cd "$(dirname "$0")/.."

if docker info > /dev/null 2>&1; then
  ENGINE=docker
elif command -v container > /dev/null 2>&1; then
  ENGINE=container
else
  echo "test.sh: needs Docker or Apple's container" >&2
  exit 1
fi

exec $ENGINE run --rm -v "$PWD:/src:ro" debian:stable-slim sh -euc '
  apt-get -qq update > /dev/null
  apt-get -qq install -y --no-install-recommends gcc libc6-dev make \
    e2fsprogs dosfstools mtools > /dev/null
  cp -r /src /work && cd /work
  make -s test TEST_DIR=/tmp/images OBJ=/tmp/obj
'
