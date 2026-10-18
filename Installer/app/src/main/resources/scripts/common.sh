
DL=@DOWNLOADS@
BYNAME=@BYNAME@
BLOCKDIR=@BLOCKDIR@
[ @REQUIRE_ROOT@ = 0 ] || [ "$(id -u)" = 0 ] || fail "This isn't running as root."
LOOSE=@LOOSE@

sha() { sha256sum "$1" | cut -d' ' -f1; }
shan() { head -c "$2" "$1" | sha256sum | cut -d' ' -f1; }
bytes() { wc -c < "$1" | tr -d ' '; }

slot() {
  for d in $BYNAME; do
    [ -e "$d/abl_$1" ] && { echo "$d/abl_$1"; return 0; }
  done
  return 1
}

# The two ABL partitions, by their exact names, as separate block devices of
# a believable size.
need_slots() {
  SA=$(slot a) && SB=$(slot b) || fail "The partitions abl_a and abl_b weren't found."
  bd=$(readlink -f "$BLOCKDIR")
  for n in "$SA" "$SB"; do
    real=$(readlink -f "$n")
    case $real in "$bd"/*) ;; *) fail "$n isn't under $BLOCKDIR." ;; esac
    [ -b "$real" ] || [ "$LOOSE" = 1 ] || fail "$n isn't a block device."
  done
  [ "$(readlink -f "$SA")" != "$(readlink -f "$SB")" ] || fail "abl_a and abl_b are one partition."
  ZA=$(blockdev --getsize64 "$SA" 2> /dev/null)
  ZB=$(blockdev --getsize64 "$SB" 2> /dev/null)
  for z in "$ZA" "$ZB"; do
    case $z in ''|*[!0-9]*) fail "Couldn't read a partition size." ;; esac
    [ "$z" -gt 0 ] && [ "$z" -le 16777216 ] || fail "An ABL partition has an odd size."
  done
}

chip_ok() {
  m=$(getprop ro.soc.model | tr a-z A-Z)
  p=$(getprop ro.board.platform | tr A-Z a-z)
  for x in @MODELS@; do
    case $m in "$x"*) return 0 ;; esac
  done
  for x in @PLATFORMS@; do
    [ "$p" = "$x" ] && return 0
  done
  return 1
}

# Both slots, whole, to a new folder in Download/OpenABL, then read back.
do_backup() {
  BDIR=$DL/OpenABL/backup-@STAMP@
  [ ! -e "$BDIR" ] || fail "The folder backup-@STAMP@ is already there."
  mkdir -p "$BDIR" || fail "Couldn't make the backup folder."
  for s in a b; do
    say working "Saving abl_$s"
    dd if="$(slot $s)" of="$BDIR/abl_$s.img" bs=4096 conv=notrunc 2> /dev/null || fail "Couldn't save abl_$s."
  done
  sync
  say working "Checking the backup"
  for s in a b; do
    [ "$(sha "$BDIR/abl_$s.img")" = "$(sha "$(slot $s)")" ] || fail "The saved abl_$s doesn't match the partition."
  done
  (cd "$BDIR" && sha256sum abl_a.img abl_b.img > SHA256SUMS && sha256sum -c SHA256SUMS > /dev/null 2>&1) ||
    fail "The backup's SHA256SUMS didn't check."
  printf '%s\n' @INFO@ > "$BDIR/info.txt"
  o=$(own "$DL")
  chown -R "$o" "$DL/OpenABL" 2> /dev/null
  restorecon -R "$DL/OpenABL" 2> /dev/null
  put backup "backup-@STAMP@"
}

# Active slot's partner first: XBL never falls back, so the slot it boots
# from is written last.
slot_order() {
  case $(getprop ro.boot.slot_suffix) in _b) echo "a b" ;; *) echo "b a" ;; esac
}

# write_slot FILE LENGTH SHA256 SLOT: write, read back, once more if it differs.
write_slot() {
  dev=$(slot "$4")
  n=0
  while [ $n -lt 2 ]; do
    n=$((n + 1))
    say working "Writing abl_$4"
    dd if="$1" of="$dev" bs=4096 conv=notrunc 2> /dev/null
    sync
    say working "Checking abl_$4"
    [ "$(shan "$dev" "$2")" = "$3" ] && return 0
  done
  return 1
}
