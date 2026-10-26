#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective
# Builds the ABL with the menu program for one or more chipsets:
#
#   ./build.sh <chipset...|all> [--debug] [--no-standard-boot] [--clean]
#
# Needs git and Docker. The sources are fetched by Scripts/fetch.sh; the
# program (Makefile) and the ABL (Scripts/abl.sh) are built in the tianocore
# container. Images go to build/out/abl_signed-<CHIPSET>.elf, the full log
# to build/abl-build.log.
set -euo pipefail
cd "$(dirname "$0")"

# The firmware version, MAJOR.MINOR: the menu, the ABL, fastboot and the
# ESRT all report it.
VERSION=$(cat VERSION)
[[ $VERSION =~ ^[0-9]+\.[0-9]+$ ]] || { echo "VERSION must be MAJOR.MINOR, not '$VERSION'" >&2; exit 1; }

IMAGE=ghcr.io/tianocore/containers/fedora-40-dev:latest
# The ABL tree's path in the container; its build cache stores this path.
WORKSPACE=/workspaces/LinuxLoader
CHIPSETS="sdm845 sm4450 sm6115 sm6450 sm7675 sm8250 sm8475 sm8550 sm8650 sm8750"

usage() {
  echo "usage: ./build.sh <chipset...|all> [--debug] [--no-standard-boot] [--clean]" >&2
  echo "chipsets: $CHIPSETS" >&2
  exit 1
}

# Per-chipset settings, as in Qualcomm's make_abl.sh: signing version,
# protected VM firmware, whether XBL has the kernel protocol (SDM845's
# doesn't), whether to bundle edk2's USB host stack (where XBL has none),
# extra ABL make parameters, and extra program features.
chipset() {
  SIGN=-v7 PVMFW=true KERNEL_PROTOCOL=1 BUNDLED_USB=0 ABL_EXTRA="" OWN="" OWN_FULL=""
  case $1 in
    sdm845) SIGN=-v5 PVMFW=false KERNEL_PROTOCOL=0 OWN=devinfo-settings,loader-quirks ;;
    sm4450) OWN=sdcc-sm4450 ;;
    sm6115) SIGN=-v6 PVMFW=false BUNDLED_USB=1 ABL_EXTRA=TARGET_SOC_SM6115=1 OWN=loader-quirks ;;
    sm6450) OWN=sdcc-sm6450 ;;
    sm7675) OWN=sdcc-sm7675 ;;
    sm8475) PVMFW=false ;;
    sm8550) ;;
    sm8650) OWN=park-display ;;
    sm8750) OWN=park-display OWN_FULL=sysfont ;;
    sm8250) SIGN=-v6 PVMFW=false ;;
    *) echo "unknown chipset $1" >&2; usage ;;
  esac
}

DEBUG=0 STANDARD_BOOT=1 CLEAN=0 ASKED=()
while [ $# -gt 0 ]; do
  case $1 in
    --debug) DEBUG=1 ;;
    --no-standard-boot) STANDARD_BOOT=0 ;;
    --clean) CLEAN=1 ;;
    -*) echo "unknown option $1" >&2; usage ;;
    all) ASKED+=($CHIPSETS) ;;
    *) ASKED+=("$(echo "$1" | tr A-Z a-z)") ;;
  esac
  shift
done
[ ${#ASKED[@]} -gt 0 ] || usage
for soc in "${ASKED[@]}"; do chipset "$soc"; done

command -v git > /dev/null || { echo "git is needed" >&2; exit 1; }
docker info > /dev/null 2>&1 || { echo "Docker isn't running" >&2; exit 1; }

Scripts/fetch.sh
mkdir -p build/out

# Build image: the tianocore image plus the packages the build needs. Its
# tag is a hash of its inputs, so it is only rebuilt when they change.
mkdir -p build/image
cp build/edk2/pip-requirements.txt build/image/ 2> /dev/null || : > build/image/pip-requirements.txt
cat > build/image/Dockerfile << EOF
FROM $IMAGE
RUN dnf --assumeyes -q install glibc-devel.i686 python3-setuptools && dnf clean all
RUN pip install -q fonttools==4.67.0
COPY pip-requirements.txt /tmp/
RUN pip install -q -r /tmp/pip-requirements.txt || true
RUN git config --global --add safe.directory '*'
EOF
TAG=openabl-build:$(cat build/image/Dockerfile build/image/pip-requirements.txt | cksum | cut -d' ' -f1)
if ! docker image inspect "$TAG" > /dev/null 2>&1; then
  echo "build: making the build image $TAG (once)"
  docker build -q --platform linux/amd64 -t "$TAG" build/image > /dev/null
fi

# Script for the container: build the program, then the ABL, per chipset.
COMMON=(
  "PREBUILT_HOST_TOOLS=BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"
  QSPA_BOOTCONFIG_ENABLE=0 AB_RETRYCOUNT_DISABLE=0 VERIFIED_BOOT_ENABLED=1
  USER_BUILD_VARIANT=1 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 TARGET_ARCHITECTURE=arm64
  CLANG_PREFIX=aarch64-linux-gnu- BOARD_BOOTLOADER_PRODUCT_NAME=OpenABL
  BOARD_BOOTLOADER_PRODUCT_VERSION="$VERSION" REMOVE_CARVEOUT_REGION=0 ABL_USB_HOST=1
  OPENABL_MENU=1
)
SCRIPT="exec 2>&1
export ABL_CLEAN=$CLEAN
. /openabl/Scripts/abl.sh
"
for soc in "${ASKED[@]}"; do
  chipset "$soc"
  features=""
  [ "$STANDARD_BOOT" = 1 ] && features="standard-boot,card-logo${OWN_FULL:+,$OWN_FULL}"
  for f in $OWN $([ "$DEBUG" = 1 ] && echo debug); do
    features="${features:+$features,}$f"
  done
  params=("${COMMON[@]}" "PVMFW_ENABLED=$PVMFW" "DISABLE_KERNEL_PROTOCOL=$((1 - KERNEL_PROTOCOL))")
  [ "$BUNDLED_USB" = 1 ] && params+=(ABL_USB_HOST_BUNDLED_STACK=1)
  [ -n "$ABL_EXTRA" ] && params+=("$ABL_EXTRA")
  quoted=$(printf " '%s'" "${params[@]}")
  SCRIPT+="echo \"Building the program for $soc ($features)\"
make -s -C /openabl FEATURES='$features' OBJ=build/obj-abl OUT=build/menu/$soc || { echo \"ERROR: the program didn't build for $soc.\"; exit 1; }
build $soc /openabl/build/menu/$soc/openabl-menu.efi $SIGN$quoted || exit 1
"
done

# Progress lines to show; everything else goes only to the log.
show() {
  grep --line-buffered -E "Building the program|Starting build for Chipset|SUCCESS:|ERROR|error:|NOTE:|> Incremental build failed|KiB\)" |
    grep --line-buffered -v "^expr:" | sed 's/^/  /'
}

mtime() { stat -c %Y "$1" 2> /dev/null || stat -f %m "$1"; }

# Stops container $1 once the log has been silent for ten minutes.
watchdog() {
  while sleep 10; do
    if [ $(($(date +%s) - $(mtime "$LOG"))) -gt 600 ]; then
      touch build/.silent
      docker kill "$1" > /dev/null 2>&1 || true
      return
    fi
  done
}

LOG=build/abl-build.log
: > "$LOG"
STARTED=$(date +%s)
echo "build: ${ASKED[*]} in $TAG (log: $LOG)"
# Under emulation the edk2 build tool sometimes hangs with no output. Stop
# the container and retry once; incremental builds pick up where it was.
status=1
for attempt in 1 2; do
  name=openabl-build-$$-$attempt
  rm -f build/.silent
  watchdog "$name" &
  dog=$!
  set +e
  docker run --rm --name "$name" --platform linux/amd64 \
    -v "$PWD:/openabl" -v "$PWD/build/edk2:$WORKSPACE" \
    -v "$PWD/build/qtestsign:/qtestsign:ro" -v "$PWD/build/out:/out" \
    -w "$WORKSPACE" "$TAG" bash -lc "$SCRIPT" 2>&1 | tee -a "$LOG" | show
  status=${PIPESTATUS[0]}
  set -e
  kill "$dog" 2> /dev/null || true
  wait "$dog" 2> /dev/null || true
  [ -f build/.silent ] || break
  echo "build: no output for 10 minutes; the container was stopped"
  status=1
done
rm -f build/.silent

echo "build: $((($(date +%s) - STARTED) / 60)) minutes"
for soc in "${ASKED[@]}"; do
  elf=build/out/abl_signed-$(echo "$soc" | tr a-z A-Z).elf
  if [ -f "$elf" ] && [ "$(mtime "$elf")" -ge "$STARTED" ]; then
    echo "  $elf ($(($(wc -c < "$elf") / 1024)) KiB) sha256 $(cut -d' ' -f1 "$elf.sha256")"
  else
    echo "  $soc: not built"
  fi
done
[ "$status" = 0 ] || { echo "build: the build failed; see $LOG" >&2; exit 1; }

for soc in "${ASKED[@]}"; do
  upper=$(echo "$soc" | tr a-z A-Z)
  Scripts/build-recovery-flashable.zip "build/out/abl_signed-$upper.elf" "build/out/openabl-recovery-flashable-$soc.zip"
done
