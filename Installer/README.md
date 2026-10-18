# OpenABL Installer

An Android app that installs OpenABL from the handheld's own Android, to
`abl_a` and `abl_b`, without a PC. It stops there: it installs no other
system.

It needs root. Without it the app saves a one-line script in Download for
you to run as root yourself.

## Using it

Open the app. It shows the chip, the model, how it gets root and what each
slot holds ("OpenABL 1.0", "OpenABL 1.0 build" for a build that isn't a
release, or "Not OpenABL"). *Install OpenABL* asks first,
then:

1. Backs up both slots to `Download/OpenABL/backup-<date>_<time>/` (`abl_a.img`,
   `abl_b.img`, `SHA256SUMS` and `info.txt`) and checks the copies. If this
   fails nothing is written.
2. Writes the image for the chip to the slot the device isn't running
   first, then the other, and reads each back. A slot that doesn't match is
   written back from the backup.

*Restore a backup* lists the backups. Restore checks one against its
`SHA256SUMS` and writes it back to both slots; it only takes backups made on
the same model. Delete removes the folder.

*Reboot to bootloader* restarts into the bootloader, OpenABL's menu once it
is installed. The version at the top right checks for a newer installer and
opens its release page when there is one.

Supported chipsets are those of the images in the release: SDM845, SM4450,
SM6115, SM6450, SM7675, SM8250, SM8550, SM8650 and SM8750. The chip comes
from `ro.soc.model`, or from `ro.board.platform` if that is empty. The app
only writes when the partitions are named `abl_a` and `abl_b` under
`/dev/block/by-name`, are separate block devices, and are at most 16 MiB.
A model that OpenABL doesn't list gets a warning but can go on.

After installing, hold Volume Down at power-on for the menu. The main
README has the keys.

## Root

The app looks for these in order and never runs anything just to find out:

| Route | How |
|---|---|
| `/product/bin/xsu` | AYANEO and KONKR firmware. Any app may run `xsu sh <script>`. |
| `PServerBinder` | Retroid's root service (`/system/bin/pservice`). The app calls it through the binder and checks it with `id -u`. |
| `su` | Magisk or similar, when `su` is in the path. Android asks you to allow it. |
| Script | The app saves `openabl-installer.sh` in Download with the line `sh <path to the job>`. Run it with the handheld's own tool, on Retroid Settings, Handheld Settings, Advanced, Run script as root. |

Each job is a shell script (`app/src/main/resources/scripts/`) that copies
itself to `/data/local/tmp/openabl-installer/`, detaches and writes its
progress to a status file the app reads.

## Updates

The APK holds the images of the release it was built for. When online it
asks GitHub for the latest OpenABL release and, if that is newer, offers to
download `openabl-<tag>.tar.gz`. The download is only kept if the release's
`SHA256SUMS` lists the archive and every image in it and they all match.
A release without `SHA256SUMS` can't be installed by the app. Nothing is
downloaded without you asking.

`SHA256SUMS` comes from the same release, so it guards against a bad
download, not against a changed release.

## Building

Needs JDK 17 or later and the Android SDK (platform 36).

```bash
./gradlew assembleDebug test
```

The first build fetches `openabl-v<VERSION>.tar.gz` from the release, checks
it against `release.sha256` and puts the images in `app/src/main/assets`
(not committed). `-PimagesDir=../build/out` uses your own build instead.
`known-images.txt` lists the hashes of released images, so slots holding an
older OpenABL are still recognised.

A release APK is signed when `OPENABL_KEYSTORE`, `OPENABL_KEYSTORE_PASSWORD`,
`OPENABL_KEY_ALIAS` and `OPENABL_KEY_PASSWORD` are set, and unsigned
otherwise. GitHub Actions (`.github/workflows/installer.yml`) builds it for
each release and uploads `openabl-installer-<TAG>.apk` and its `.sha256`.
It needs the repository secrets `INSTALLER_KEYSTORE` (the keystore, base64),
`INSTALLER_KEYSTORE_PASSWORD`, `INSTALLER_KEY_ALIAS` and
`INSTALLER_KEY_PASSWORD`.

## Licence

GPL-3.0-or-later like the rest of OpenABL. The Montserrat font is OFL-1.1;
`LICENSES.md` lists the rest.
