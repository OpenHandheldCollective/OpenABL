NAME=@FOLDER@

case $NAME in
  backup-[0-9]*) ;;
  *) fail "That isn't a backup folder." ;;
esac
case $NAME in
  */*|*..*) fail "That isn't a backup folder." ;;
esac
BDIR=$DL/OpenABL/$NAME
[ -d "$BDIR" ] || fail "That backup is already gone."
say working "Deleting the backup"
rm -rf "$BDIR" || fail "Couldn't delete the backup."
put deleted "$NAME"
say done "Deleted $NAME"
