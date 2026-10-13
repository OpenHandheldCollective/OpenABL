/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gpt - GPT parsing and rewriting for Uninstall CFW.

  Uninstall removes every partition after userdata and grows userdata to
  the last usable block in both tables. Android reclaims the space on its
  next factory reset.
*/

#ifndef __CORE_GPT_H__
#define __CORE_GPT_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Base/Text.h"

#define GPT_SIGNATURE "EFI PART"

// Userdata type 1B81E7E6-F50D-419B-A739-2AEEF8DA3335, restored on uninstall.
extern CONST UINT8 gGptAndroidUserdata[16];
// Linux filesystem type 0FC63DAF-8483-4772-8E79-3D69D8477DE4.
extern CONST UINT8 gGptLinuxFs[16];

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

// Returns TRUE if Linux is installed: an entry after userdata is in use.
BOOLEAN
GptCfwAfterUserdata (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen
);

typedef struct _GPT_WRITE {
  UINT64      Lba;
  OABL_BUFFER Data;
} GPT_WRITE;

// Blocks to write, in order.
typedef struct _GPT_WRITES OABL_VEC (GPT_WRITE) GPT_WRITES;

VOID
GptWritesFree (IN OUT GPT_WRITES *Writes);

// Clears every entry after userdata, restores userdata's Android type and
// grows it to the last usable block. Header is the primary header block and
// Array the entries in whole blocks. Free Out with GptWritesFree.
BOOLEAN
GptUninstall (
  IN CONST UINT8 *Header,
  IN UINTN        HeaderLen,
  IN CONST UINT8 *Array,
  IN UINTN        ArrayLen,
  IN UINTN        BlockSize,
  OUT GPT_WRITES *Out
);

// Name is UTF-8, cut to 36 UTF-16 units. For GptBuild, Blocks 0 means up
// to the last usable block.
typedef struct _GPT_PART {
  UINT8        Type[16];
  CONST CHAR8 *Name;
  UINT64       Blocks;
} GPT_PART;

// For tests: shrinks userdata and adds Parts in the space freed at its end,
// as an install to internal storage does.
BOOLEAN
GptInstallAfterUserdata (
  IN CONST UINT8    *Header,
  IN UINTN           HeaderLen,
  IN CONST UINT8    *Array,
  IN UINTN           ArrayLen,
  IN UINTN           BlockSize,
  IN CONST GPT_PART *Parts,
  IN UINTN           PartCount,
  OUT GPT_WRITES    *Out
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
