#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective
# Puts the images of the release named in ../VERSION into the app's assets,
# with the table of image hashes the app identifies slots by. Downloads the
# release archive once (checked against release.sha256), or takes the images
# from a folder, such as build/out.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
version=$(cat "$here/../VERSION")
assets=$here/app/src/main/assets
cache=$here/.cache
from=${1:-}

sha() {
  if command -v sha256sum > /dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

rm -rf "$assets/images"
mkdir -p "$assets/images" "$cache"

if [ -n "$from" ]; then
  cp "$from"/abl_signed-*.elf "$assets/images/"
else
  name=openabl-v$version.tar.gz
  want=$(grep " $name\$" "$here/release.sha256" | cut -d' ' -f1)
  [ -n "$want" ] || { echo "release.sha256 has no hash for $name" >&2; exit 1; }
  if [ ! -f "$cache/$name" ] || [ "$(sha "$cache/$name")" != "$want" ]; then
    curl -fsSL -o "$cache/$name" \
      "https://github.com/OpenHandheldCollective/OpenABL/releases/download/v$version/$name"
  fi
  [ "$(sha "$cache/$name")" = "$want" ] || { echo "$name does not match release.sha256" >&2; exit 1; }
  rm -rf "$cache/unpacked"
  mkdir "$cache/unpacked"
  tar -xzf "$cache/$name" -C "$cache/unpacked"
  cp "$cache"/unpacked/*/*/abl_signed-*.elf "$assets/images/"
fi

cp "$here/known-images.txt" "$assets/known-images.txt"
for elf in "$assets"/images/abl_signed-*.elf; do
  soc=$(basename "$elf" .elf)
  soc=${soc#abl_signed-}
  hash=$(sha "$elf")
  size=$(wc -c < "$elf" | tr -d ' ')
  line=$(grep "^$version $soc " "$assets/known-images.txt" || true)
  if [ -z "$line" ]; then
    echo "$version $soc $hash $size" >> "$assets/known-images.txt"
  elif [ "$line" != "$version $soc $hash $size" ]; then
    echo "$soc differs from known-images.txt" >&2
    exit 1
  fi
done

mkdir -p "$assets/licenses"
cp "$here/../Assets/Montserrat-OFL.txt" "$assets/licenses/"
# The MIT text is for Tabler Icons; Apache's appendix is a template.
sed 's/^Copyright (c) <year> <copyright holders>$/Copyright (c) 2020-2024 Paweł Kuna/' "$here/../LICENSES/MIT.txt" > "$assets/licenses/MIT.txt"
sed '/^ *APPENDIX: How to apply/,$d' "$here/../LICENSES/Apache-2.0.txt" > "$assets/licenses/Apache-2.0.txt"
