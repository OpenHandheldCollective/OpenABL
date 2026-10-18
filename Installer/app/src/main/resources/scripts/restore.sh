
NAME=@FOLDER@
MODEL=@MODEL@

case $NAME in
  backup-[0-9]*) ;;
  *) fail "That isn't a backup folder." ;;
esac
case $NAME in
  */*|*..*) fail "That isn't a backup folder." ;;
esac
BDIR=$DL/OpenABL/$NAME

need_slots
for f in SHA256SUMS info.txt abl_a.img abl_b.img; do
  [ -f "$BDIR/$f" ] || fail "The backup has no $f."
done
grep -qxF "Model: $MODEL" "$BDIR/info.txt" || fail "This backup is from another handheld."
say working "Checking the backup"
(cd "$BDIR" && sha256sum -c SHA256SUMS > /dev/null 2>&1) || fail "The backup doesn't match its SHA256SUMS."
for s in a b; do
  grep -q " abl_$s.img\$" "$BDIR/SHA256SUMS" || fail "SHA256SUMS doesn't list abl_$s.img."
done
[ "$(bytes "$BDIR/abl_a.img")" = "$ZA" ] && [ "$(bytes "$BDIR/abl_b.img")" = "$ZB" ] ||
  fail "The images don't fit the partitions."

for s in $(slot_order); do
  write_slot "$BDIR/abl_$s.img" "$(bytes "$BDIR/abl_$s.img")" "$(sha "$BDIR/abl_$s.img")" $s ||
    fail "abl_$s didn't read back right."
  put slot_$s ok
done
say done "Both slots are restored"
