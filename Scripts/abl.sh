# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective

# Builds and signs one ABL image. Sourced in the build container, from the
# ABL's tree:
#
#   build IMAGE MENU SIGN CHIP-IDS MAKE-PARAM...
#
# CHIP-IDS (comma separated ChipInfo ids) go in the image tag. Each image
# keeps its own build tree, moved into place while it builds because the
# generated makefiles contain absolute paths. The tree is rebuilt from
# scratch when the make parameters change, with ABL_CLEAN=1, or when an
# incremental build fails. The signed image goes to /out.

build() {
  name=$1; menu=$2; sign=$3; ids=$4; shift 4
  echo "------------------------------------------------"
  echo "Starting build for image: $name..."
  B=obj/ABL_OBJ/Build/RELEASE_CLANG35
  if [ -d "$B" ]; then
    owner=$(cat "$B/.image" 2>/dev/null)
    if [ -n "$owner" ] && [ "$owner" != "$name" ]; then rm -rf "$B.$owner"; mv "$B" "$B.$owner"
    elif [ -z "$owner" ]; then rm -rf "$B"; fi
  fi
  if [ ! -d "$B" ] && [ -d "$B.$name" ]; then mv "$B.$name" "$B"; fi
  rm -f unsigned_abl.elf
  # Use Conf from BaseTools' templates, not the Conf in Qualcomm's tree
  # (which links with lld through FUSE_LD).
  for c in build_rule tools_def target; do
    cmp -s "BaseTools/Conf/$c.template" "Conf/$c.txt" || { . edksetup.sh --reconfig; break; }
  done
  mkdir -p QcomModulePkg/Prebuilt/OpenAblMenu
  cp "$menu" QcomModulePkg/Prebuilt/OpenAblMenu/openabl-menu.efi
  # Image tag for firmware updates (Core/AblImage.h).
  python3 -c 'import struct, sys; ids = [int(i) for i in sys.argv[3].split(",")]; sys.stdout.buffer.write(b"OABLIMG1" + b"".join(a.encode().ljust(16, b"\0")[:16] for a in sys.argv[1:3]) + struct.pack("<%dI" % (len(ids) + 1), len(ids), *ids))' \
    "$(echo "$name" | tr a-z A-Z)" "$(cat /openabl/VERSION)" "$ids" > QcomModulePkg/Prebuilt/OpenAblMenu/image-tag.bin
  if [ "$ABL_CLEAN" = 1 ] || [ "$(cat "$B/.make-args" 2>/dev/null)" != "$*" ]; then rm -rf "$B"; fi
  incremental=0; [ -d "$B" ] && incremental=1
  mkdir -p "$B"; echo "$name" > "$B/.image"; echo "$*" > "$B/.make-args"
  if ! make all "$@"; then
    [ "$incremental" = 1 ] || { echo "ERROR: Build failed for $name."; return 1; }
    echo "  > Incremental build failed; building $name from scratch"
    rm -rf "$B" unsigned_abl.elf; mkdir -p "$B"
    echo "$name" > "$B/.image"; echo "$*" > "$B/.make-args"
    make all "$@" || { echo "ERROR: Build failed for $name."; return 1; }
  fi
  out=abl_signed-$(echo "$name" | tr a-z A-Z).elf
  python3 /qtestsign/qtestsign.py "$sign" -o "/out/$out" abl unsigned_abl.elf || { echo "ERROR: Signing failed for $name."; return 1; }
  (cd /out && sha256sum "$out" > "$out.sha256")
  echo "SUCCESS: $out"
}

# copies IMAGE CHIPSET...: a copy of the image under each chipset's name.
copies() {
  from=abl_signed-$(echo "$1" | tr a-z A-Z).elf; shift
  for chip in "$@"; do
    to=abl_signed-$(echo "$chip" | tr a-z A-Z).elf
    cp "/out/$from" "/out/$to" && (cd /out && sha256sum "$to" > "$to.sha256") || return 1
  done
}
