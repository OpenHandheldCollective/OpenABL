/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootEntries - finds what each filesystem offers to boot.

  Every FAT filesystem is searched for Boot Loader Specification entries and
  UKIs (sorted as systemd-boot sorts them), extlinux.conf labels, and the
  default file \EFI\BOOT\BOOTAA64.EFI. Removable media come first, so an SD
  card or USB drive overrides internal storage.
*/

#ifndef __BOOT_BOOT_ENTRIES_H__
#define __BOOT_BOOT_ENTRIES_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Base/Text.h"
#include "Core/BootEntry.h"

// One filesystem and its boot entries. Owns all its members.
typedef struct _BOOT_ENTRIES_VOLUME {
  EFI_HANDLE Fs;
  // Device path of the partition, or of the whole disk.
  OABL_BUFFER Dev;
  // Volume label, or "drive N" if there is none.
  CHAR8  *Name;
  BOOLEAN Removable;
  // GPT partition GUID in LoaderDevicePartUUID format, or NULL.
  CHAR8 *PartUuid;
  // \EFI\BOOT\BOOTAA64.EFI exists here and is not our own image.
  BOOLEAN DefaultFile;
  // loader.conf's default pattern, or NULL.
  CHAR8 *LoaderDefault;
  // Entries in boot order: the default first, then menu order, with
  // entries that have no tries left at the end.
  BOOT_ENTRY_LIST Entries;
} BOOT_ENTRIES_VOLUME;

typedef struct _BOOT_ENTRIES_VOLUMES
  OABL_VEC (BOOT_ENTRIES_VOLUME) BOOT_ENTRIES_VOLUMES;

// Returns "removable drive" or "internal drive".
CONST CHAR8 *
BootEntriesVolumeKind (IN CONST BOOT_ENTRIES_VOLUME *V);

// Returns the volume's menu name, such as "Linux (removable drive)".
// Caller frees the result.
CHAR8 *
BootEntriesVolumeDescribe (IN CONST BOOT_ENTRIES_VOLUME *V);

VOID
BootEntriesVolumeFree (IN OUT BOOT_ENTRIES_VOLUME *V);

VOID
BootEntriesVolumesFree (IN OUT BOOT_ENTRIES_VOLUMES *Volumes);

// Scans every filesystem into Out, removable media first. With HasOnly,
// scans only the OnlyCount handles in Only, in that order. OwnImage is our
// own device path, so our file is not taken for a default file. With
// DefaultFiles, \EFI\BOOT\BOOTAA64.EFI counts as a target (the ABL starts
// those itself).
VOID
BootEntriesDiscover (
  IN OABL_BYTES             OwnImage,
  IN BOOLEAN                HasOnly,
  IN CONST EFI_HANDLE      *Only OPTIONAL,
  IN UINTN                  OnlyCount,
  IN BOOLEAN                DefaultFiles,
  OUT BOOT_ENTRIES_VOLUMES *Out
);

// Entry Entry of drive Volume.
typedef struct _BOOT_ENTRIES_REF {
  UINTN Volume;
  UINTN Entry;
} BOOT_ENTRIES_REF;

typedef struct _BOOT_ENTRIES_REFS
  OABL_VEC (BOOT_ENTRIES_REF) BOOT_ENTRIES_REFS;

// Returns TRUE if drives A and B of Volumes are partitions of one disk. A
// drive with no HD() node is a disk of its own.
BOOLEAN
BootEntriesSameDisk (
  IN CONST BOOT_ENTRIES_VOLUME *Volumes,
  IN UINTN                      A,
  IN UINTN                      B
);

// Returns TRUE if drive Index is the first of its disk in Volumes.
BOOLEAN
BootEntriesDiskStart (
  IN CONST BOOT_ENTRIES_VOLUME *Volumes,
  IN UINTN                      Index
);

// Appends the entries of the disk that starts at drive Disk, in boot order:
// the Type #1 entries and UKIs of all its partitions ranked together, the
// default first (by the first loader.conf on the disk that names one), then
// each partition's other entries. So A/B entries on separate partitions
// boot as one list, as systemd-boot sees them.
VOID
BootEntriesDiskOrder (
  IN CONST BOOT_ENTRIES_VOLUME *Volumes,
  IN UINTN                      Count,
  IN UINTN                      Disk,
  IN OUT BOOT_ENTRIES_REFS     *Out
);

#endif
