#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective
# Builds the ABL with the menu program, one image per MBN version:
#
#   ./build.sh <image|chipset...|all> [--debug] [--no-standard-boot] [--clean]
#
# A chipset builds its image. Needs git and Docker. The sources are
# fetched by Scripts/fetch.sh; the program (Makefile) and the ABL
# (Scripts/abl.sh) are built in the tianocore container. Images go to
# build/out/abl_signed-<IMAGE>.elf, the full log to build/abl-build.log.
set -euo pipefail
cd "$(dirname "$0")"

# The firmware version, MAJOR.MINOR: the menu, the ABL, fastboot and the
# ESRT all report it.
VERSION=$(cat VERSION)
[[ $VERSION =~ ^[0-9]+\.[0-9]+$ ]] || { echo "VERSION must be MAJOR.MINOR, not '$VERSION'" >&2; exit 1; }

IMAGE=ghcr.io/tianocore/containers/fedora-40-dev:latest
# The ABL tree's path in the container; its build cache stores this path.
WORKSPACE=/workspaces/LinuxLoader
IMAGES="mbn5 mbn6 mbn7"
CHIPSETS="sdm845 sm4450 sm6115 sm6450 sm7675 sm7750 sm8250 sm8475 sm8550 sm8650 sm8750"

usage() {
  echo "usage: ./build.sh <image|chipset...|all> [--debug] [--no-standard-boot] [--clean]" >&2
  echo "images: $IMAGES" >&2
  echo "chipsets: $CHIPSETS" >&2
  exit 1
}

# Each chipset's image and ChipInfo ids (Linux's soc_id). Chips signed with
# the same MBN version share an image; per-chip fixes check the id at
# runtime.
chipset() {
  case $1 in
    sdm845) OF=mbn5 CHIP_IDS=321,341 ;;
    sm6115) OF=mbn6 CHIP_IDS=444,467 ;;
    sm8250) OF=mbn6 CHIP_IDS=356 ;;
    sm4450) OF=mbn7 CHIP_IDS=654 ;;
    sm6450) OF=mbn7 CHIP_IDS=634 ;;
    sm7675) OF=mbn7 CHIP_IDS=700 ;;
    sm7750) OF=mbn7 CHIP_IDS=659,732 ;;
    sm8475) OF=mbn7 CHIP_IDS=531 ;;
    sm8550) OF=mbn7 CHIP_IDS=601,603 ;;
    sm8650) OF=mbn7 CHIP_IDS=682 ;;
    sm8750) OF=mbn7 CHIP_IDS=639,706 ;;
    *) return 1 ;;
  esac
}

image_of() {
  case $1 in
    mbn5 | mbn6 | mbn7) echo "$1" ;;
    *) chipset "$1" || { echo "unknown image or chipset $1" >&2; usage; }; echo "$OF" ;;
  esac
}

# Per-image settings, as in Qualcomm's make_abl.sh: signing version,
# protected VM firmware, whether XBL has the kernel protocol (SDM845's
# doesn't), whether to bundle edk2's USB host stack (SM6115's XBL has none),
# and extra program features. Also sets OWNERS (its chipsets) and IDS
# (their ids).
image() {
  case $1 in
    mbn5) SIGN=-v5 PVMFW=false KERNEL_PROTOCOL=0 BUNDLED_USB=0 OWN=loader-quirks OWN_FULL="" ;;
    mbn6) SIGN=-v6 PVMFW=false KERNEL_PROTOCOL=1 BUNDLED_USB=1 OWN=loader-quirks OWN_FULL="" ;;
    mbn7) SIGN=-v7 PVMFW=true KERNEL_PROTOCOL=1 BUNDLED_USB=0
      OWN=sdcc-sm4450,sdcc-sm6450,sdcc-sm7675 OWN_FULL=sysfont ;;
  esac
  OWNERS="" IDS=""
  for c in $CHIPSETS; do
    chipset "$c"
    if [ "$OF" = "$1" ]; then
      OWNERS="${OWNERS:+$OWNERS }$c"
      IDS="${IDS:+$IDS,}$CHIP_IDS"
    fi
  done
}

upper() { echo "$1" | tr a-z A-Z; }

DEBUG=0 STANDARD_BOOT=1 CLEAN=0 ASKED=()
while [ $# -gt 0 ]; do
  case $1 in
    --debug) DEBUG=1 ;;
    --no-standard-boot) STANDARD_BOOT=0 ;;
    --clean) CLEAN=1 ;;
    -*) echo "unknown option $1" >&2; usage ;;
    all) ASKED+=($IMAGES) ;;
    *)
      name=$(image_of "$(echo "$1" | tr A-Z a-z)")
      [[ " ${ASKED[*]} " == *" $name "* ]] || ASKED+=("$name")
      ;;
  esac
  shift
done
[ ${#ASKED[@]} -gt 0 ] || usage

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

# Script for the container: build the program, then the ABL, per image.
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
for name in "${ASKED[@]}"; do
  image "$name"
  features=""
  [ "$STANDARD_BOOT" = 1 ] && features="standard-boot,card-logo${OWN_FULL:+,$OWN_FULL}"
  for f in ${OWN//,/ } $([ "$DEBUG" = 1 ] && echo debug); do
    features="${features:+$features,}$f"
  done
  params=("${COMMON[@]}" "PVMFW_ENABLED=$PVMFW" "DISABLE_KERNEL_PROTOCOL=$((1 - KERNEL_PROTOCOL))")
  [ "$BUNDLED_USB" = 1 ] && params+=(ABL_USB_HOST_BUNDLED_STACK=1)
  quoted=$(printf " '%s'" "${params[@]}")
  SCRIPT+="echo \"Building the program for $name ($features)\"
make -s -C /openabl FEATURES='$features' OBJ=build/obj-abl OUT=build/menu/$name || { echo \"ERROR: the program didn't build for $name.\"; exit 1; }
build $name /openabl/build/menu/$name/openabl-menu.efi $SIGN $IDS$quoted || exit 1
copies $name $OWNERS || exit 1
"
done

# Progress lines to show; everything else goes only to the log.
show() {
  grep --line-buffered -E "Building the program|Starting build for image|SUCCESS:|ERROR|error:|NOTE:|> Incremental build failed|KiB\)" |
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
for name in "${ASKED[@]}"; do
  image "$name"
  elf=build/out/abl_signed-$(upper "$name").elf
  if [ -f "$elf" ] && [ "$(mtime "$elf")" -ge "$STARTED" ]; then
    echo "  $elf ($(($(wc -c < "$elf") / 1024)) KiB) sha256 $(cut -d' ' -f1 "$elf.sha256")"
    echo "    copied as $(for c in $OWNERS; do printf 'abl_signed-%s.elf ' "$(upper "$c")"; done)"
  else
    echo "  $name: not built"
  fi
done
[ "$status" = 0 ] || { echo "build: the build failed; see $LOG" >&2; exit 1; }

# Every file with its chipsets and soc_ids, for distributions.
MANIFEST=build/out/openabl-manifest.tsv
{
  echo "# OpenABL $VERSION. chip_ids are /sys/devices/soc0/soc_id values."
  echo "# Each chipset's file is a copy of its MBN image."
  printf 'file\tversion\tmbn\tchipsets\tchip_ids\tsha256\n'
  for name in "${ASKED[@]}"; do
    image "$name"
    file=abl_signed-$(upper "$name").elf
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$file" "$VERSION" "${SIGN#-v}" \
      "$(upper "${OWNERS// /,}")" "$IDS" "$(cut -d' ' -f1 "build/out/$file.sha256")"
    for c in $OWNERS; do
      chipset "$c"
      file=abl_signed-$(upper "$c").elf
      printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$file" "$VERSION" "${SIGN#-v}" \
        "$(upper "$c")" "$CHIP_IDS" "$(cut -d' ' -f1 "build/out/$file.sha256")"
    done
  done
} > "$MANIFEST"
echo "  $MANIFEST"

for name in "${ASKED[@]}"; do
  image "$name"
  zip=build/out/openabl-recovery-flashable-$name.zip
  Scripts/build-recovery-flashable.zip "build/out/abl_signed-$(upper "$name").elf" "$zip" "$IDS"
  for c in $OWNERS; do
    cp "$zip" "build/out/openabl-recovery-flashable-$c.zip"
  done
done
