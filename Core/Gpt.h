/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gpt - GPT parsing and rewriting for installing PortareOS on internal
  storage.

  Installing makes PortareOS's boot and storage partitions. Dualboot
  shrinks userdata and puts them after it; Replace Android removes
  Android's system and data partitions, with any PortareOS already there,
  and gives PortareOS all of their space.
*/

#ifndef __CORE_GPT_H__
#define __CORE_GPT_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Base/Text.h"

#define GPT_SIGNATURE "EFI PART"

// Linux filesystem type 0FC63DAF-8483-4772-8E79-3D69D8477DE4.
extern CONST UINT8 gGptLinuxFs[16];
// EFI system partition type C12A7328-F81F-11D2-BA4B-00A0C93EC93B, which
// PortareOS's boot partition has.
extern CONST UINT8 gGptEfiSystem[16];

// PortareOS's partitions, by name.
#define GPT_BOOT_NAME    "PORTAREOS"
#define GPT_STORAGE_NAME "PORTAREOS_STOR"

typedef struct _GPT_HEADER {
  UINT32 HeaderSize;
  UINT64 MyLba;
  UINT64 AlternateLba;
  UINT64 FirstUsable;
  UINT64 LastUsable;
  UINT64 EntryLba;
  UINT32 Entries;
  UINT32 EntrySize;
} GPT_HEADER;

// Parses a header. Checks the signature and sizes, but not the CRCs.
BOOLEAN
GptParse (
  IN CONST UINT8 *Block,
  IN UINTN        Len,
  OUT GPT_HEADER *Out
);

UINTN
GptArraySize (IN CONST GPT_HEADER *Header);

// Returns TRUE if Linux is installed: an entry in use lies on disk after
// the one userdata. FALSE when there are two userdata entries.
BOOLEAN
GptCfwAfterUserdata (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen
);

// Returns TRUE if entry Index is in use and called Name (ASCII).
BOOLEAN
GptEntryNamed (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN UINTN             Index,
  IN CONST CHAR8      *Name
);

typedef struct _GPT_WRITE {
  UINT64      Lba;
  OABL_BUFFER Data;
} GPT_WRITE;

// Blocks to write, in order.
typedef struct _GPT_WRITES OABL_VEC (GPT_WRITE) GPT_WRITES;

VOID
GptWritesFree (IN OUT GPT_WRITES *Writes);

// Name is UTF-8, cut to 36 UTF-16 units. For GptBuild, Blocks 0 means up
// to the last usable block.
typedef struct _GPT_PART {
  UINT8        Type[16];
  CONST CHAR8 *Name;
  UINT64       Blocks;
} GPT_PART;

// PortareOS on a disk: its boot and storage partitions' first and last
// blocks, and the boot partition's unique GUID, which identifies the
// installation at boot.
typedef struct _GPT_PORTAREOS {
  UINT64 BootFirst;
  UINT64 BootLast;
  UINT64 StorageFirst;
  UINT64 StorageLast;
  UINT8  BootGuid[16];
} GPT_PORTAREOS;

// What a disk holds, for the install choices.
typedef struct _GPT_SURVEY {
  // Android: super and one userdata.
  BOOLEAN Android;
  UINT64  UserdataFirst;
  UINT64  UserdataLast;
  // One of each of PortareOS's partitions, after userdata if Android is
  // there.
  BOOLEAN       PortareOs;
  GPT_PORTAREOS Found;
  // Dualboot can shrink userdata: Android with nothing after userdata.
  BOOLEAN CanShrink;
  // Replace Android can take everything from ReplaceFirst on: Android's
  // and PortareOS's partitions, with nothing else among or after them.
  BOOLEAN CanReplace;
  UINT64  ReplaceFirst;
  UINT64  LastUsable;
} GPT_SURVEY;

VOID
GptSurvey (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  OUT GPT_SURVEY      *Out
);

// An install's sizes in blocks, and the new partitions' unique GUIDs.
typedef struct _GPT_INSTALL {
  UINT64 AndroidBlocks; // Dualboot: what userdata keeps, at least
  UINT64 BootBlocks;
  UINT64 MinStorage;    // the storage partition's least size
  UINT64 Align;         // PortareOS's boot partition starts on a multiple
  UINT8  BootGuid[16];
  UINT8  StorageGuid[16];
} GPT_INSTALL;

// Dualboot on a disk with Android alone: userdata shrinks to at least
// AndroidBlocks, ending where the boot partition starts, on an Align
// boundary; the storage partition follows the boot partition to the last
// usable block. Header is the primary header block and Array the entries
// in whole blocks; the writes rebuild the backup table, whose array is at
// BackupEntryLba, from the primary, backup first. Free Out with
// GptWritesFree. Where gets the new partitions. FALSE unless GptSurvey says
// CanShrink, or if the storage partition would be under MinStorage.
BOOLEAN
GptInstallDualboot (
  IN CONST UINT8       *Header,
  IN UINTN              HeaderLen,
  IN CONST UINT8       *Array,
  IN UINTN              ArrayLen,
  IN UINTN              BlockSize,
  IN UINT64             BackupEntryLba,
  IN CONST GPT_INSTALL *Install,
  OUT GPT_WRITES       *Out,
  OUT GPT_PORTAREOS    *Where
);

// Replace Android: clears super, vbmeta_system_a and _b, userdata and
// PortareOS's partitions, then puts the boot partition at the first Align
// boundary from where the first of them started and the storage partition
// after it, to the last usable block. FALSE unless GptSurvey says
// CanReplace, or if the storage partition would be under MinStorage.
BOOLEAN
GptInstallReplace (
  IN CONST UINT8       *Header,
  IN UINTN              HeaderLen,
  IN CONST UINT8       *Array,
  IN UINTN              ArrayLen,
  IN UINTN              BlockSize,
  IN UINT64             BackupEntryLba,
  IN CONST GPT_INSTALL *Install,
  OUT GPT_WRITES       *Out,
  OUT GPT_PORTAREOS    *Where
);

// Parses a header and checks its signature and both CRCs.
BOOLEAN
GptVerify (
  IN CONST UINT8 *Block,
  IN UINTN        Len,
  IN CONST UINT8 *Array,
  IN UINTN        ArrayLen,
  OUT GPT_HEADER *Out
);

// Whether two verified headers are one disk's primary and backup: each
// pointing at the other, the backup at or before LastBlock, the same
// usable blocks and entry layout. CRCs pass but don't prove these.
BOOLEAN
GptCopiesAgree (
  IN CONST GPT_HEADER *Primary,
  IN CONST GPT_HEADER *Backup,
  IN UINT64            LastBlock
);

// For tests: builds a disk image with Parts laid out in order from the
// first usable block. Caller frees Out.
BOOLEAN
GptBuild (
  IN UINT64          DiskBlocks,
  IN UINTN           BlockSize,
  IN CONST GPT_PART *Parts,
  IN UINTN           PartCount,
  OUT OABL_BUFFER   *Out
);

// Reads and verifies the table at HeaderLba. Array points into Disk.
BOOLEAN
GptReadBack (
  IN CONST UINT8 *Disk,
  IN UINTN        DiskLen,
  IN UINTN        BlockSize,
  IN UINT64       HeaderLba,
  OUT GPT_HEADER *Header,
  OUT OABL_BYTES *Array
);

// Returns entry Index's type, if the entry is in use.
BOOLEAN
GptEntryType (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN UINTN             Index,
  OUT UINT8            Type[16]
);

// Copies entry Index's unique GUID, if the entry is in use.
BOOLEAN
GptEntryGuid (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN UINTN             Index,
  OUT UINT8            Guid[16]
);

// Returns entry Index's first and last block, if the entry is in use.
BOOLEAN
GptExtent (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN UINTN             Index,
  OUT UINT64          *Start,
  OUT UINT64          *End
);

#endif
