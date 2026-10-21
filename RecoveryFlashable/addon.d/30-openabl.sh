#!/sbin/sh
#
# ADDOND_VERSION=3
#
# /system/addon.d/30-openabl.sh
. /tmp/backuptool.functions

log_msg() {
  echo "OpenABL: $*" >&2
}

list_files() { echo "product/etc/openabl.img"; }
opposite_slot() { slot="$(getprop ro.boot.slot_suffix)"; [ -n "$slot" ] || slot="_$(getprop ro.boot.slot)"; case "$slot" in _a|a) echo b;; _b|b) echo a;; *) return 1;; esac; }
flash_opposite_abl() {
  slot="$(opposite_slot)" || { log_msg "could not determine opposite slot"; return 1; }
  abl="/dev/block/by-name/abl_$slot"
  image="$(get_output_path "$S/product/etc/openabl.img")"

  [ -s "$image" ] || { log_msg "image is missing or empty"; return 1; }
  [ -b "$abl" ] || { log_msg "target block device is missing"; return 1; }

  image_size="$(stat -c %s "$image")" || return 1
  target_size="$(blockdev --getsize64 "$abl")" || return 1
  [ "$image_size" -le "$target_size" ] || { log_msg "image exceeds target size"; return 1; }
  original_ro="$(blockdev --getro "$abl")" || { log_msg "could not read target read-only flag"; return 1; }
  case "$original_ro" in 0|1) ;; *) log_msg "unexpected target read-only flag: $original_ro"; return 1;; esac
  if [ "$original_ro" = 1 ]; then
    if ! rw_output="$(blockdev --setrw "$abl" 2>&1)"; then
      log_msg "could not clear target read-only flag: $rw_output"
      return 1
    fi
  fi

  flash_status=0
  if [ "$(blockdev --getro "$abl")" != 0 ]; then
    log_msg "target is not writable after clearing read-only flag"
    flash_status=1
  else
    if dd_output="$(dd if="$image" of="$abl" bs=4M conv=fsync 2>&1)"; then
      # Compare only the image length; the rest of the partition is untouched.
      if ! verify_output="$(cmp -n "$image_size" "$image" "$abl" 2>&1)"; then
        log_msg "verification failed for $abl: $verify_output"
        flash_status=1
      fi
    else
      dd_status=$?
      log_msg "failed to write $abl: dd exit=$dd_status output=$dd_output"
      flash_status=1
    fi
  fi

  if [ "$original_ro" = 1 ]; then
    if ! ro_output="$(blockdev --setro "$abl" 2>&1)" || [ "$(blockdev --getro "$abl")" != 1 ]; then
      log_msg "could not restore target read-only flag: $ro_output"
      flash_status=1
    fi
  fi
  return "$flash_status"
}
case "$1" in
  backup) list_files | while read -r FILE DUMMY; do backup_file "$S/$FILE"; done ;;
  restore) list_files | while read -r FILE REPLACEMENT; do R=""; [ -n "$REPLACEMENT" ] && R="$S/$REPLACEMENT"; [ -f "$C/$S/$FILE" ] && restore_file "$S/$FILE" "$R"; done ;;
  pre-backup|post-backup|pre-restore) ;;
  post-restore)
    list_files | while read -r FILE DUMMY; do
      f="$(get_output_path "$S/$FILE")"
      if [ ! -f "$f" ]; then
        log_msg "restored file is missing: $f"
        continue
      fi
      chown root:root "$f"
      chmod 0644 "$f"
      chmod 0755 "$(dirname "$f")"
    done
    flash_opposite_abl || exit 1
  ;;
esac
