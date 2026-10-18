
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
for d in "$DL"/OpenABL/backup-*; do
  [ -f "$d/SHA256SUMS" ] && put backup "$(basename "$d")"
done
say done "Read both slots"
