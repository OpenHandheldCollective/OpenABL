/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Bls - Boot Loader Specification entries, loader.conf and boot counting.

  Ordering and version comparison follow systemd-boot, so both loaders pick
  the same default from the same files.

  https://uapi-group.org/specifications/specs/boot_loader_specification/
  https://systemd.io/AUTOMATIC_BOOT_ASSESSMENT/
*/

#ifndef __CORE_BLS_H__
#define __CORE_BLS_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Base/Text.h"
#include "Core/BootEntry.h"

#define BLS_ENTRIES_DIR "\\loader\\entries"
#define BLS_LOADER_CONF "\\loader\\loader.conf"
#define BLS_UKI_DIR     "\\EFI\\Linux"

// Value of the architecture key that matches this loader.
#define BLS_ARCH "aa64"

// Returns the counter after one more try. As in systemd-boot, an entry with
// no tries left still counts the try when it is booted as a last resort.
BLS_COUNTER
BlsCounterBumped (IN BLS_COUNTER Counter);

// Formats the counter for the log: "Some(Counter { left: 3, done: 0 })", or
// "None" when Some is FALSE. 64 bytes are always enough.
VOID
BlsCounterDebug (
  IN BOOLEAN     Some,
  IN BLS_COUNTER Counter,
  OUT CHAR8     *Buffer,
  IN UINTN       BufferSize
);

// Splits a file name into id and counter: debian+3-1.conf gives debian.conf
// with 3 left and 1 done. Suffix is matched ignoring case, since FAT keeps
// whatever case the file was written with.
CHAR8 *
BlsSplitCounter (
  IN CONST CHAR8  *File,
  IN CONST CHAR8  *Suffix,
  OUT BOOLEAN     *Counted,
  OUT BLS_COUNTER *Counter
);

// Returns debian+2-1.conf for debian.conf with 2 left and 1 done.
CHAR8 *
BlsCountedName (
  IN CONST CHAR8 *Id,
  IN BLS_COUNTER  Counter
);

// Compares versions like systemd's strverscmp_improved (UAPI Version Format
// Specification): '~' sorts first, '-' separates version from release,
// numbers beat letters, and more segments mean newer.
INT32
BlsVerCmp (
  IN CONST CHAR8 *VersionA,
  IN CONST CHAR8 *VersionB
);

// Matches Name against a glob of '*', '?' and [...] ([!...] or [^...]).
BOOLEAN
BlsGlob (
  IN CONST CHAR8 *Pattern,
  IN CONST CHAR8 *Name
);

// Keys of a Type #1 entry file. Absent keys are NULL.
typedef struct _BLS_TYPE1 {
  CHAR8 *Title;
  CHAR8 *Version;
  CHAR8 *MachineId;
  CHAR8 *SortKey;
  // The linux key. Not named Linux since compilers may define that macro.
  CHAR8             *Kernel;
  BOOT_ENTRY_STRINGS Initrd;
  CHAR8             *Efi;
  CHAR8             *Uki;
  BOOT_ENTRY_STRINGS Options;
  CHAR8             *Devicetree;
  BOOT_ENTRY_STRINGS DevicetreeOverlay;
  CHAR8             *Architecture;
} BLS_TYPE1;

// Parses "key value" lines. initrd and options may repeat; for other keys
// the last one wins.
BOOLEAN
BlsParseType1 (
  IN OABL_TEXT   Text,
  OUT BLS_TYPE1 *Out
);

VOID
BlsType1Free (IN OUT BLS_TYPE1 *Type1);

// Fails if the entry is for another architecture than Arch or has nothing
// to start.
BOOLEAN
BlsType1Entry (
  IN CONST CHAR8 *FileName,
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Arch,
  OUT BOOT_ENTRY *Out
);

typedef struct _BLS_KV {
  CHAR8 *Key;
  CHAR8 *Value;
} BLS_PAIR;

typedef struct _BLS_KVS OABL_VEC (BLS_PAIR) BLS_PAIRS;

// Parses the KEY=value pairs of an os-release file, quotes stripped.
BOOLEAN
BlsOsRelease (
  IN OABL_TEXT   Text,
  OUT BLS_PAIRS *Out
);

VOID
BlsPairsFree (IN OUT BLS_PAIRS *Pairs);

// Makes an entry from a UKI's .osrel and .uname sections. Uname is {NULL, 0}
// if the UKI has none.
BOOLEAN
BlsUkiEntry (
  IN CONST CHAR8 *FileName,
  IN OABL_TEXT    Osrel,
  IN OABL_TEXT    Uname,
  OUT BOOT_ENTRY *Out
);

// Returns the default key of loader.conf, a glob over entry ids, or NULL.
CHAR8 *
BlsLoaderConfDefault (IN OABL_TEXT Text);

// What the timeout key of loader.conf asks for.
typedef enum {
  BLS_MENU_HIDDEN,  // 0, menu-hidden or menu-disabled: boot the default now
  BLS_MENU_TIMEOUT, // show the entries, then boot the default after a while
  BLS_MENU_FORCE,   // menu-force: show the entries until one is picked
} BLS_MENU;

// Parses a timeout value: seconds, menu-force, menu-hidden or
// menu-disabled. Fails on anything else, leaving Menu and Seconds as they
// were.
BOOLEAN
BlsParseTimeout (
  IN OABL_TEXT  Value,
  OUT BLS_MENU *Menu,
  OUT UINT32   *Seconds
);

// Reads the timeout key of loader.conf. Fails if it is absent or invalid.
BOOLEAN
BlsLoaderConfTimeout (
  IN OABL_TEXT  Text,
  OUT BLS_MENU *Menu,
  OUT UINT32   *Seconds
);

// Compares in systemd-boot's menu order: entries with no tries left go
// last, entries with a sort-key first (by sort-key, machine-id, newest
// version), then newest id, then more tries left and fewer done.
INT32
BlsCompare (
  IN CONST BOOT_ENTRY *A,
  IN CONST BOOT_ENTRY *B
);

// Stable sort in BlsCompare order.
VOID
BlsSort (
  IN OUT BOOT_ENTRY *Entries,
  IN UINTN           Count
);

// Picks the first entry with tries left whose id matches Pattern, else the
// first with tries left, else the first. Fails on an empty list.
BOOLEAN
BlsPickDefault (
  IN CONST BOOT_ENTRY *Entries,
  IN UINTN             Count,
  IN CONST CHAR8      *Pattern OPTIONAL,
  OUT UINTN           *Out
);

#endif
