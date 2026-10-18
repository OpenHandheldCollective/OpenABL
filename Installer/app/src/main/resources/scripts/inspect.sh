
need_slots
say working "Reading the slots"
put active "$(getprop ro.boot.slot_suffix)"
put size_a "$ZA"
put size_b "$ZB"
for s in a b; do
  for n in @LENGTHS@; do
    put hash_${s}_$n "$(shan "$(slot $s)" "$n")"
  done
done
# For the app to read the version of builds that aren't a release.
for s in a b; do
  c=$(dirname "$STATUS")/slot_$s.img
  head -c 1048576 "$(slot $s)" > "$c" || continue
  [ -z "$OWNER" ] || chown "$OWNER" "$c" 2> /dev/null
  chmod 660 "$c"
done
for d in "$DL"/OpenABL/backup-*; do
  [ -f "$d/SHA256SUMS" ] && put backup "$(basename "$d")"
done
say done "Read both slots"
