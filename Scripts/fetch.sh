#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective
# Fetches the build's sources into build/, each at a pinned commit:
#
#   Scripts/fetch.sh [microui] [edk2] [qtestsign]   (default: all three)
#
# build/microui    microui, which lays out the menu
# build/edk2       Qualcomm's ABL source (tianocore edk2 from CodeLinaro) with
#                  Abl/openabl.patch applied and Abl/QcomModulePkg's files
#                  added. When either changes, the tree is reset and patched
#                  again; build products are kept.
# build/qtestsign  the image signer
set -euo pipefail
cd "$(dirname "$0")/.."

EDK2_URL=https://git.codelinaro.org/clo/la/abl/tianocore/edk2.git
EDK2_TAG=KERNEL.PLATFORM.3.0.r23-02400-kernel.0
EDK2_COMMIT=aef4e86045bd151a54d33bab8ae5e8e62462a8f4
MICROUI_URL=https://github.com/rxi/microui.git
MICROUI_COMMIT=0850aba860959c3e75fb3e97120ca92957f9d057
QTESTSIGN_URL=https://github.com/msm8916-mainline/qtestsign.git
QTESTSIGN_COMMIT=f3df53a5f0e37ccee076541f30f9d5b8340fc2d3
PATCH=Abl/openabl.patch
FILES=Abl/QcomModulePkg
# Build products in the ABL tree, kept when it is re-patched.
BUILD_PRODUCTS=(/obj /Output /unsigned_abl.elf /build_modulepkg.log /BaseTools/Source/C /QcomModulePkg/Prebuilt)

# Shallow-clones URL into DIR at COMMIT, fetching REF if needed.
fetch() {
  local dir=$1 url=$2 ref=$3 commit=$4
  if [ ! -d "$dir/.git" ]; then
    mkdir -p "$dir"
    git -C "$dir" init -q
    git -C "$dir" remote add origin "$url"
  fi
  if ! git -C "$dir" rev-parse -q --verify "$commit^{commit}" > /dev/null; then
    echo "fetch: $url ($ref)"
    git -C "$dir" fetch -q --depth 1 origin "$ref"
  fi
  if [ "$(git -C "$dir" rev-parse -q --verify "$commit^{commit}")" != "$commit" ]; then
    echo "fetch: $url: $ref isn't $commit" >&2
    exit 1
  fi
}

microui() {
  fetch build/microui "$MICROUI_URL" "$MICROUI_COMMIT" "$MICROUI_COMMIT"
  if [ "$(git -C build/microui rev-parse HEAD 2> /dev/null)" != "$MICROUI_COMMIT" ]; then
    git -C build/microui -c advice.detachedHead=false checkout -q --detach "$MICROUI_COMMIT"
  fi
}

edk2() {
  fetch build/edk2 "$EDK2_URL" "refs/tags/$EDK2_TAG:refs/tags/$EDK2_TAG" "$EDK2_COMMIT"
  local stamp
  stamp="$EDK2_COMMIT $(git hash-object "$PATCH") $( (find "$FILES" -type f | sort; find "$FILES" -type f | sort | xargs git hash-object) | git hash-object --stdin)"
  if [ "$(cat build/edk2/.git/openabl-patch 2> /dev/null)" = "$stamp" ]; then
    return
  fi
  echo "fetch: applying $PATCH to Qualcomm's $EDK2_TAG"
  rm -f build/edk2/.git/openabl-patch
  git -C build/edk2 checkout -q -f --detach "$EDK2_COMMIT"
  local keep=()
  for p in "${BUILD_PRODUCTS[@]}"; do keep+=(-e "$p"); done
  git -C build/edk2 clean -q -f -d -x "${keep[@]}"
  git -C build/edk2 apply --whitespace=nowarn "$PWD/$PATCH"
  cp -R "$FILES/." build/edk2/QcomModulePkg/
  echo "$stamp" > build/edk2/.git/openabl-patch
}

qtestsign() {
  fetch build/qtestsign "$QTESTSIGN_URL" "$QTESTSIGN_COMMIT" "$QTESTSIGN_COMMIT"
  if [ "$(git -C build/qtestsign rev-parse HEAD 2> /dev/null)" != "$QTESTSIGN_COMMIT" ]; then
    git -C build/qtestsign -c advice.detachedHead=false checkout -q --detach "$QTESTSIGN_COMMIT"
  fi
}

[ $# -gt 0 ] || set -- microui edk2 qtestsign
for what in "$@"; do
  case $what in
    microui | edk2 | qtestsign) "$what" ;;
    *) echo "fetch: unknown $what (microui, edk2, qtestsign)" >&2; exit 1 ;;
  esac
done
