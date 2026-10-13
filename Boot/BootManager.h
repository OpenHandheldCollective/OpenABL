/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootManager - decides what to boot, and in what order.

  We are both the UEFI boot manager (UEFI 2.10 section 3.1, EBBR) and a Boot
  Loader Specification loader. The order is BootNext (deleted first),
  LoaderEntryOneShot, LoaderEntryDefault, the active BootOrder options, and
  then every drive's entries and default file, removable drives first. A
  Boot#### option naming a whole drive stands for that drive's targets.
  Firmware-volume options and anything that would start us again are skipped.
*/

#ifndef __BOOT_BOOT_MANAGER_H__
#define __BOOT_BOOT_MANAGER_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Boot/BootEntries.h"

typedef enum {
  // An EFI image by full device path, from a Boot#### option or a drive's
  // default file.
  BOOT_MANAGER_TARGET_EFI,
  // Boot entry Entry of drive Volume.
  BOOT_MANAGER_TARGET_ENTRY,
} BOOT_MANAGER_TARGET_KIND;

// One thing to try. Fields used only by the other kind are zero. Owns its
// label and buffers.
typedef struct _BOOT_MANAGER_TARGET {
  BOOT_MANAGER_TARGET_KIND Kind;
  // Boot#### number, if the target came from a load option.
  BOOLEAN     HasOption;
  UINT16      Option;
  CHAR8      *Label;
  OABL_BUFFER Path;
  // Load options, from the Boot#### option's optional data.
  OABL_BUFFER Options;
  // The target's drive. Always set for entries, and for EFI targets when
  // known.
  BOOLEAN HasVolume;
  UINTN   Volume;
  // Index into the drive's Entries.
  UINTN Entry;
} BOOT_MANAGER_TARGET;

typedef struct _BOOT_MANAGER_TARGETS
  OABL_VEC (BOOT_MANAGER_TARGET) BOOT_MANAGER_TARGETS;

VOID
BootManagerTargetFree (IN OUT BOOT_MANAGER_TARGET *Target);

VOID
BootManagerTargetsFree (IN OUT BOOT_MANAGER_TARGETS *Targets);

typedef struct _BOOT_MANAGER {
  // Full device path of our own image, so we never start ourselves. Empty
  // if unknown.
  OABL_BUFFER OwnImage;
} BOOT_MANAGER;

// Initializes Out for the running image. Finds OwnImage through the image's
// LoadedImage protocol, opened exclusively.
VOID
BootManagerNew (OUT BOOT_MANAGER *Out);

VOID
BootManagerFree (IN OUT BOOT_MANAGER *Manager);

STATIC inline OABL_BYTES
BootManagerOwnImage (IN CONST BOOT_MANAGER *Manager)
{
  return BufferBytes (&Manager->OwnImage);
}

// Lists the targets to try on Volumes, in order. Deletes the one-shot
// variables. Free Out with BootManagerTargetsFree.
VOID
BootManagerPlan (
  IN CONST BOOT_MANAGER        *Manager,
  IN CONST BOOT_ENTRIES_VOLUME *Volumes,
  IN UINTN                      VolumeCount,
  OUT BOOT_MANAGER_TARGETS     *Out
);

// Sets BootCurrent to Option for the image about to start. Deletes it when
// HasOption is FALSE.
VOID
BootManagerSetBootCurrent (
  IN BOOLEAN HasOption,
  IN UINT16  Option
);

#endif
