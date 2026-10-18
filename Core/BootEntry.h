/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootEntry - a boot entry the loader can start by itself.

  Boot Loader Specification entries (Type #1 files and Type #2 Unified Kernel
  Images) and extlinux.conf labels all become a BOOT_ENTRY. An entry owns its
  strings; optional ones are NULL.
*/

#ifndef __CORE_BOOT_ENTRY_H__
#define __CORE_BOOT_ENTRY_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Base/Text.h"

typedef enum {
  BOOT_ENTRY_SOURCE_TYPE1,
  BOOT_ENTRY_SOURCE_UKI,
  BOOT_ENTRY_SOURCE_EXTLINUX,
} BOOT_ENTRY_SOURCE;

// Name of the source for the log.
CONST CHAR8 *
BootEntrySourceDebug (IN BOOT_ENTRY_SOURCE Source);

// The +LEFT-DONE try counter in an entry's file name. Defined here so this
// header doesn't need Bls.h.
typedef struct _BLS_COUNTER {
  UINT32 Left;
  UINT32 Done;
} BLS_COUNTER;

typedef struct _BOOT_ENTRY_STRINGS
OABL_VEC (CHAR8 *) BOOT_ENTRY_STRINGS;

VOID
BootEntryStringsFree (IN OUT BOOT_ENTRY_STRINGS *Strings);

typedef enum {
  // A Linux kernel with the EFI stub. The command line becomes its load
  // options and the initrds are served, concatenated, through the
  // LINUX_EFI_INITRD_MEDIA LoadFile2 handle.
  BOOT_ENTRY_ACTION_LINUX,
  // An EFI program (a UKI, or the efi or uki program of a Type #1 entry),
  // started with Options as its load options.
  BOOT_ENTRY_ACTION_EFI,
} BOOT_ENTRY_ACTION_KIND;

// Fields of the kind not in use are NULL or empty.
typedef struct _BOOT_ENTRY_ACTION {
  BOOT_ENTRY_ACTION_KIND Kind;
  // Linux
  CHAR8             *Kernel;
  BOOT_ENTRY_STRINGS Initrds;
  // Never NULL.
  CHAR8             *Cmdline;
  BOOT_ENTRY_STRINGS Overlays;
  // extlinux FDTDIR: where to look for the selected model's devicetree.
  CHAR8 *FdtDir;
  // Efi
  CHAR8 *Path;
  CHAR8 *Options;
  // Both
  CHAR8 *Devicetree;
} BOOT_ENTRY_ACTION;

typedef struct _BOOT_ENTRY {
  // Boot Loader Specification id: the file name without its boot counter
  // (debian-6.12.conf, fedora.efi), or extlinux:<label>.
  CHAR8            *Id;
  CHAR8            *Title;
  CHAR8            *Version;
  CHAR8            *SortKey;
  CHAR8            *MachineId;
  BOOT_ENTRY_SOURCE Source;
  BOOLEAN           HasCounter;
  BLS_COUNTER       Counter;
  // The file that describes the entry. Boot counting renames it.
  CHAR8            *File;
  BOOT_ENTRY_ACTION Action;
} BOOT_ENTRY;

typedef struct _BOOT_ENTRY_LIST OABL_VEC (BOOT_ENTRY) BOOT_ENTRY_LIST;

VOID
BootEntryActionFree (IN OUT BOOT_ENTRY_ACTION *Action);

VOID
BootEntryFree (IN OUT BOOT_ENTRY *Entry);

VOID
BootEntryListFree (IN OUT BOOT_ENTRY_LIST *Entries);

// Deep-copies Entry. Out is left empty on failure.
BOOLEAN
BootEntryClone (
  IN CONST BOOT_ENTRY *Entry,
  OUT BOOT_ENTRY      *Out
);

// Returns TRUE if the entry has no tries left.
BOOLEAN
BootEntryIsBad (IN CONST BOOT_ENTRY *Entry);

// Returns TRUE for Type #1 and Type #2 entries, those in LoaderEntries.
BOOLEAN
BootEntryIsBls (IN CONST BOOT_ENTRY *Entry);

// Turns a specification path (/vmlinuz, EFI/Linux/a.efi) into a UEFI path
// from the filesystem root (\vmlinuz).
CHAR8 *
BootEntryUefiPath (IN OABL_TEXT Path);

// Applies BootEntryUefiPath to each string. Out is left empty on failure.
BOOLEAN
BootEntryUefiPaths (
  IN CONST BOOT_ENTRY_STRINGS *In,
  OUT BOOT_ENTRY_STRINGS      *Out
);

// Returns the directory part of a UEFI path: \loader\entries\a.conf gives
// \loader\entries.
OABL_TEXT
BootEntryParent (IN CONST CHAR8 *Path);

OABL_TEXT
BootEntryFileName (IN CONST CHAR8 *Path);

// Entry file parsing helpers

// Splits a "key value" line at its first ASCII whitespace and trims the
// value. A line without whitespace is all key.
VOID
BootEntryKeyValue (
  IN OABL_TEXT   Line,
  OUT OABL_TEXT *Key,
  OUT OABL_TEXT *Value
);

BOOLEAN
BootEntryFirstWord (
  IN OABL_TEXT   Text,
  OUT OABL_TEXT *Word
);

// Appends the non-empty pieces of Text, split where IsSeparator is TRUE.
// On failure, pieces already added stay in List.
BOOLEAN
BootEntryPushWords (
  IN OUT BOOT_ENTRY_STRINGS *List,
  IN OABL_TEXT               Text,
  IN BOOLEAN (*IsSeparator) (IN CHAR8 C)
);

// Replaces *Field with a copy of Value, so the last key wins.
BOOLEAN
BootEntrySetText (
  IN OUT CHAR8 **Field,
  IN OABL_TEXT   Value
);

// Sets Out to Path as a UEFI path, or NULL if Path is NULL.
BOOLEAN
BootEntryOptUefiPath (
  OUT CHAR8     **Out,
  IN CONST CHAR8 *Path OPTIONAL
);

#endif
