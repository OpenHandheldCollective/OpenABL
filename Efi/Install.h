/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Install - copies PortareOS from the SD card to internal storage.

  InstallSurvey looks at both: the card's PortareOS boot partition, and the
  internal disk's partition table, whose two copies must agree. InstallRun
  then installs one of two ways:

  - Dualboot keeps Android. On a first install userdata shrinks, which
    resets Android: its start is zeroed, so Android formats it on its next
    start. With PortareOS already next to Android, the layout stays and
    only the boot partition is rewritten; storage, with the games and
    saves, is kept.
  - Replace Android removes Android's system and data partitions, with any
    PortareOS already there, and gives PortareOS all of their space.

  In order: what gets replaced erased, through the firmware's handles for
  the partitions as they were (as fastboot erase does); the new table,
  backup first, read back; the new partitions' starts zeroed; the storage
  file system (Ext4Make);
  the card's files copied to the boot partition and read back; last the
  boot partition's FATs and boot sector (Fat32Make), so an interrupted
  install doesn't leave a boot partition that mounts. The SD card is only
  read: the target is a fixed disk that isn't the card's.
*/

#ifndef __EFI_INSTALL_H__
#define __EFI_INSTALL_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Core/Gpt.h"
#include "Efi/Serial.h"

typedef enum {
  INSTALL_DUALBOOT,
  INSTALL_REPLACE_ANDROID,
} INSTALL_KIND;

// The size of PortareOS's boot partition, as installtointernal makes it.
#define INSTALL_BOOT_BYTES (2ull << 30)

// What the internal disk holds, read once and then remembered until an
// install. FALSE if no single fixed disk holds Android or PortareOS.
BOOLEAN
InstallInternal (OUT GPT_SURVEY *Out);

typedef struct _INSTALL_SURVEY {
  // A PortareOS card: its boot partition with KERNEL and SYSTEM, and the
  // size of the files to copy.
  BOOLEAN Card;
  UINT64  CardBytes;
  // The internal disk's table, read and checked.
  BOOLEAN    Internal;
  UINT32     BlockSize;
  GPT_SURVEY Gpt;
} INSTALL_SURVEY;

// Looks for the card and reads the internal disk's table.
VOID
InstallSurvey (
  OUT INSTALL_SURVEY *Out,
  IN LOG_SINK         Log
);

// Whether Kind can install onto what Survey found. Doesn't look at the
// card.
BOOLEAN
InstallCan (
  IN CONST INSTALL_SURVEY *Survey,
  IN INSTALL_KIND          Kind
);

// Whether Dualboot keeps PortareOS's storage: it's already next to Android.
BOOLEAN
InstallKeepsStorage (IN CONST INSTALL_SURVEY *Survey);

// What a first Dualboot can leave Android, in bytes: from Min to Max,
// both whole GiB. FALSE if Survey can't shrink userdata.
BOOLEAN
InstallAndroidRange (
  IN CONST INSTALL_SURVEY *Survey,
  OUT UINT64              *Min,
  OUT UINT64              *Max
);

// What PortareOS's storage gets, in bytes: with Kind, and for a first
// Dualboot with AndroidBytes left to Android.
UINT64
InstallStorageBytes (
  IN CONST INSTALL_SURVEY *Survey,
  IN INSTALL_KIND          Kind,
  IN UINT64                AndroidBytes
);

typedef enum {
  INSTALL_STEP_PREPARE, // the table, discards
  INSTALL_STEP_STORAGE, // the storage file system
  INSTALL_STEP_COPY,    // the card's files
  INSTALL_STEP_CHECK,   // reading them back
} INSTALL_STEP;

// Called as the install goes on: the step, and how much of the whole is
// done, in thousandths.
typedef VOID (*INSTALL_PROGRESS) (
  IN INSTALL_STEP Step,
  IN UINT32       Permille,
  IN VOID        *Context
);

// Installs. AndroidBytes is what a first Dualboot leaves Android. Nothing
// is written when it returns EFI_NOT_FOUND (no card or files on it),
// EFI_VOLUME_CORRUPTED (the internal table can't be trusted or doesn't
// allow Kind), EFI_NOT_READY (the battery is too low to flash) or
// EFI_VOLUME_FULL (the files don't fit). EFI_CRC_ERROR
// means something written didn't read back the same; anything else is the
// failing read or write's status.
EFI_STATUS
InstallRun (
  IN INSTALL_KIND     Kind,
  IN UINT64           AndroidBytes,
  IN INSTALL_PROGRESS Progress,
  IN VOID            *Context,
  IN LOG_SINK         Log
);

#endif
