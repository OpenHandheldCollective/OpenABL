/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DevInfo - LinuxLoader's devinfo record, read to carry user settings over.

  The record is the struct in LinuxLoader's DeviceInfo.h. It has no packing
  pragma, so fields sit at their natural AArch64 alignment: 3272 bytes in
  the 4 KiB devinfo partition. It is read through Qualcomm's VerifiedBoot
  protocol (VBRwDeviceState) or straight from the partition.
*/

#ifndef __CORE_DEV_INFO_H__
#define __CORE_DEV_INFO_H__

#include "Base/OablBase.h"

#include "Base/Text.h"
#include "Core/Settings.h"

// Not NUL-terminated in the record.
#define DEVINFO_MAGIC     "ANDROID-BOOT!"
#define DEVINFO_MAGIC_LEN 13
#define DEVINFO_SIZE      3272

// Persistent value slots, and the maximum name and value sizes.
#define DEVINFO_ENTRIES   8
#define DEVINFO_NAME_MAX  56
#define DEVINFO_VALUE_MAX 32

typedef struct _DEVINFO_VALUE {
  // UTF-8, invalid bytes replaced by U+FFFD. May hold a NUL; use NameLen.
  CHAR8 Name[DEVINFO_NAME_MAX * 3 + 1];
  UINTN NameLen;
  UINT8 Value[DEVINFO_VALUE_MAX];
  UINTN ValueLen;
} DEVINFO_VALUE;

typedef struct _DEVINFO {
  BOOLEAN Unlocked;
  BOOLEAN UnlockCritical;
  BOOLEAN ChargerScreen;
  // UTF-8 up to the NUL, invalid sequences replaced.
  CHAR8         BootloaderVersion[64 * 3 + 1];
  DEVINFO_VALUE Values[DEVINFO_ENTRIES];
  UINTN         ValueCount;
} DEVINFO;

// Maps a setting's variable to the persistent value that used to hold it.
typedef struct _DEVINFO_LEGACY {
  CONST CHAR8 *Key;
  CONST CHAR8 *Name;
} DEVINFO_LEGACY;

// The persistent values that held the settings before they moved to
// UEFI variables, in the order they are moved.
#define DEVINFO_LEGACY_SETTINGS_LEN 7
extern CONST DEVINFO_LEGACY gDevInfoLegacySettings[DEVINFO_LEGACY_SETTINGS_LEN];

// Old values with no setting now, cleared along with the others.
#define DEVINFO_LEGACY_ONLY_LEN 2
extern CONST CHAR8 *CONST gDevInfoLegacyOnly[DEVINFO_LEGACY_ONLY_LEN];

// Number of variables DevInfoSettingsMove writes.
#define DEVINFO_SETTINGS_MOVE_LEN (DEVINFO_LEGACY_SETTINGS_LEN + 2)

// Fills Out with the variables that replace the devinfo settings and
// returns the count. Info is NULL if there is no record. Unset settings are
// written empty, deleting any variable left from another schema. Off-mode
// charging follows the charger-screen flag, but is on if there is no record
// or no OpenAblVer value, since the ABL turns it on in those cases. The
// schema is written last.
UINTN
DevInfoSettingsMove (
  IN CONST DEVINFO *Info OPTIONAL,
  OUT SETTINGS_VAR  Out[DEVINFO_SETTINGS_MOVE_LEN]
);

// Sets the charger-screen flag in a raw record. Fails if the record is too
// short or not written yet.
BOOLEAN
DevInfoSetChargerScreen (
  IN OUT UINT8 *Raw,
  IN UINTN      Size,
  IN BOOLEAN    On,
  OUT BOOLEAN  *Changed
);

// Zeroes the persistent values named in Names. Returns TRUE if any were
// cleared.
BOOLEAN
DevInfoClearValues (
  IN OUT UINT8          *Raw,
  IN UINTN               Size,
  IN CONST CHAR8 *CONST *Names,
  IN UINTN               NameCount
);

// Sets persistent value Name in its own entry, or else the first free one.
// Fails if it doesn't fit or no entry is free.
BOOLEAN
DevInfoSetValue (
  IN OUT UINT8   *Raw,
  IN UINTN        Size,
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Value,
  IN UINTN        ValueSize
);

// Gets a legacy setting in its variable's encoding (Out needs
// DEVINFO_VALUE_MAX bytes). The model is cut at its NUL, and boot source
// codes are swapped in pairs: devinfo has Internal 0, Auto 1, USB 2, SD
// card 3; the variable has Auto 0, Internal 1, SD card 2, USB 3.
BOOLEAN
DevInfoLegacySetting (
  IN CONST DEVINFO *Info,
  IN CONST CHAR8   *Key,
  OUT UINT8        *Out,
  OUT UINTN        *Length
);

// Fails if the record is too short or has no magic.
BOOLEAN
DevInfoParse (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  OUT DEVINFO    *Out
);

// Finds persistent value Name. Value points into Info.
BOOLEAN
DevInfoValue (
  IN CONST DEVINFO *Info,
  IN CONST CHAR8   *Name,
  OUT OABL_BYTES   *Value
);

// Encodes the record, for tests and for seeding the virtual device. Unknown
// fields stay zero.
VOID
DevInfoToBytes (
  IN CONST DEVINFO *Info,
  OUT UINT8         Out[DEVINFO_SIZE]
);

// Returns TRUE if an older ABL stored any of its settings here.
BOOLEAN
DevInfoHasLegacySettings (IN CONST DEVINFO *Info);

// Copies the older ABL's settings in the record into Settings.
VOID
DevInfoApplyOpenAblSettings (
  IN CONST DEVINFO *Info,
  IN OUT SETTINGS  *Settings
);

#endif
