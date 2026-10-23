#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective
#
# Checks the images make-test wrote to DIR: each FAT image with fsck.fat
# and its files copied out with mtools against the expected tree; each ext4
# image with e2fsck, its superblock against one mke2fs makes with the same
# settings, then a file written with debugfs and e2fsck again. Needs
# dosfstools, mtools and e2fsprogs.
#
# Usage: Tests/check.sh DIR

set -eu
DIR=$1
FAILED=0

fail() {
  echo "FAIL: $*"
  FAILED=1
}

for IMG in "$DIR"/fat-*.img; do
  NAME=$(basename "$IMG" .img)
  echo "== $NAME"
  fsck.fat -n -v "$IMG" > "$DIR/$NAME.fsck" 2>&1 || fail "$NAME: fsck.fat"
  tail -n 1 "$DIR/$NAME.fsck"
  OUT="$DIR/$NAME.out"
  rm -rf "$OUT" && mkdir "$OUT"
  MTOOLS_SKIP_CHECK=1 mcopy -s -n -i "$IMG" '::/*' "$OUT/" || fail "$NAME: mcopy"
  MTOOLS_SKIP_CHECK=1 mcopy -n -i "$IMG" '::/.hidden' "$OUT/" || fail "$NAME: mcopy .hidden"
  diff -r "$DIR/$NAME.files" "$OUT" || fail "$NAME: files differ"
  LABEL=$(MTOOLS_SKIP_CHECK=1 mlabel -s -i "$IMG" ::)
  echo "$LABEL" | grep -q "PORTAREOS" || fail "$NAME: label: $LABEL"
done

# Superblock fields that legitimately differ from mke2fs's.
VARIES='UUID|Hash Seed|Checksum seed|created|Last write time|Last mount time|Last checked|Checksum:|Lifetime writes|Directory Hash Seed|Journal checksum|Filesystem flags|Overhead clusters'

for IMG in "$DIR"/ext4-*.img; do
  NAME=$(basename "$IMG" .img)
  read -r BYTES JOURNAL < "$DIR/$NAME.txt"
  echo "== $NAME ($((BYTES >> 20)) MiB, journal $((JOURNAL * 4)) KiB)"
  e2fsck -fn "$IMG" > "$DIR/$NAME.fsck" 2>&1 || {
    cat "$DIR/$NAME.fsck"
    fail "$NAME: e2fsck"
  }
  tail -n 1 "$DIR/$NAME.fsck"
  # The same settings through mke2fs, for the superblock and groups.
  REF="$DIR/$NAME.ref"
  rm -f "$REF"
  truncate -s "$BYTES" "$REF"
  mke2fs -q -F -t ext4 -b 4096 -O ^resize_inode,^orphan_file -i 32768 -m 0 \
    -J size=$((JOURNAL * 4 / 1024)) -L PORTAREOS_STOR \
    -E lazy_itable_init=1,lazy_journal_init=1,nodiscard "$REF"
  dumpe2fs -h "$IMG" 2>/dev/null | grep -Ev "$VARIES" > "$DIR/$NAME.sb"
  dumpe2fs -h "$REF" 2>/dev/null | grep -Ev "$VARIES" > "$DIR/$NAME.ref.sb"
  diff "$DIR/$NAME.ref.sb" "$DIR/$NAME.sb" > "$DIR/$NAME.sb.diff" || {
    echo "-- superblock differs from mke2fs's (< mke2fs, > ours):"
    cat "$DIR/$NAME.sb.diff"
  }
  rm -f "$REF"
  # Use it: a directory and a file, then check again.
  head -c 300000 /dev/urandom > "$DIR/blob"
  debugfs -w -R "mkdir roms" "$IMG" > /dev/null 2>&1 || fail "$NAME: mkdir"
  debugfs -w -R "write $DIR/blob roms/blob" "$IMG" > /dev/null 2>&1 ||
    fail "$NAME: write"
  e2fsck -fn "$IMG" > "$DIR/$NAME.fsck2" 2>&1 || {
    cat "$DIR/$NAME.fsck2"
    fail "$NAME: e2fsck after writing"
  }
  debugfs -R "dump roms/blob $DIR/blob.out" "$IMG" > /dev/null 2>&1
  cmp -s "$DIR/blob" "$DIR/blob.out" || fail "$NAME: file reads back"
done

[ $FAILED -eq 0 ] && echo "check: all images pass"
exit $FAILED
