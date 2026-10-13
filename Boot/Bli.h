/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Bli - systemd's Boot Loader Interface variables.

  They tell the OS what was booted and let it ask for a one-shot entry or
  the menu (https://systemd.io/BOOT_LOADER_INTERFACE/). They are set only
  when we start a Boot Loader Specification entry ourselves. A chainloaded
  loader such as GRUB or systemd-boot sets its own.
*/

#ifndef __BOOT_BLI_H__
#define __BOOT_BLI_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

// 4a67b082-0a4c-41cf-b6c7-440b29bb8c4f
extern EFI_GUID gBliVendor;

// LoaderFeatures bits, as systemd's EFI_LOADER_FEATURE_* values.
#define BLI_FEATURE_CONFIG_TIMEOUT_ONE_SHOT ((UINT64)1 << 1)
#define BLI_FEATURE_ENTRY_DEFAULT           ((UINT64)1 << 2)
#define BLI_FEATURE_ENTRY_ONESHOT           ((UINT64)1 << 3)
#define BLI_FEATURE_BOOT_COUNTING           ((UINT64)1 << 4)
// Entries are read from every FAT filesystem, so XBOOTLDR is covered.
#define BLI_FEATURE_XBOOTLDR   ((UINT64)1 << 5)
#define BLI_FEATURE_SORT_KEY   ((UINT64)1 << 8)
#define BLI_FEATURE_DEVICETREE ((UINT64)1 << 10)

// Reads a variable as UTF-8 text. Returns NULL if it is unset.
// Caller frees the result.
CHAR8 *
BliGet (IN CONST CHAR8 *Name);

// Like BliGet, but deletes the variable after reading it.
CHAR8 *
BliTake (IN CONST CHAR8 *Name);

// Sets a volatile variable to Value as UTF-16. Failures are only logged.
VOID
BliSet (
  IN CONST CHAR8 *Name,
  IN CONST CHAR8 *Value
);

// What the OS is told about the entry being started. Optional strings may
// be NULL. All pointers are borrowed.
typedef struct _BLI_STARTED {
  // Loader name and version.
  CONST CHAR8 *LoaderInfo;
  // Microseconds since reset when the loader started.
  UINT64 InitUs;
  // Path of the running image, such as \EFI\BOOT\BOOTAA64.EFI.
  CONST CHAR8 *ImagePath;
  // GPT partition GUID of the filesystem holding the entry.
  CONST CHAR8 *PartUuid;
  // Ids of all Boot Loader Specification entries found.
  CHAR8 *CONST *Entries;
  UINTN         EntryCount;
  CONST CHAR8  *Selected;
  // The entry's new path if boot counting renamed it.
  CONST CHAR8 *CountPath;
} BLI_STARTED;

// Sets the variables describing the entry being started, and logs it.
VOID
BliReport (IN CONST BLI_STARTED *Started);

#endif
