
IMAGE=@IMAGE@
LEN=@LENGTH@
SUM=@SHA256@

need_slots
chip_ok || fail "This isn't the chip the image is for."
say working "Checking the image"
IMG=$WORK/image.elf
cp "$IMAGE" "$IMG" || fail "Couldn't read the image."
[ "$(bytes "$IMG")" = "$LEN" ] || fail "The image is the wrong size."
[ "$(sha "$IMG")" = "$SUM" ] || fail "The image doesn't match its hash."
[ "$(head -c 4 "$IMG" | tail -c 3)" = ELF ] || fail "The image isn't an ELF file."
[ "$LEN" -le "$ZA" ] && [ "$LEN" -le "$ZB" ] || fail "The image is larger than the partition."

do_backup

for s in $(slot_order); do
  if write_slot "$IMG" "$LEN" "$SUM" $s; then
    put slot_$s ok
    continue
  fi
  put slot_$s failed
  say working "Putting back abl_$s"
  dd if="$BDIR/abl_$s.img" of="$(slot $s)" bs=4096 conv=notrunc 2> /dev/null
  sync
  if [ "$(sha "$(slot $s)")" = "$(sha "$BDIR/abl_$s.img")" ]; then
    fail "abl_$s didn't read back right. The saved copy is back in place."
  fi
  fail "abl_$s didn't read back right, and the saved copy didn't go back. Restore it from $BDIR."
done
say done "Both slots hold the new image"
