/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Disk - block I/O and partition tables on fixed disks, for the installer
  and the device info, and whether Linux is on internal storage.

  Block I/O goes through page-aligned buffers, which satisfy any IoAlign.
*/

#ifndef __EFI_DISK_H__
#define __EFI_DISK_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Core/Gpt.h"
#include "Efi/Serial.h"

// Opens a disk's BlockIo shared, leaving the disk with its drivers. NULL if
// there is none.
EFI_BLOCK_IO_PROTOCOL *
DiskBlockIo (IN EFI_HANDLE Handle);

// Reads Length bytes from Lba. Caller frees Out with BufferFree.
BOOLEAN
DiskRead (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
  IN UINT64                 Lba,
  IN UINTN                  Length,
  OUT OABL_BUFFER          *Out
);

// Reads the GPT of a fixed disk with media present: the primary header's
// block (LBA 1) into Block, the parsed header into Header, and the entry
// array as whole blocks into Array. Caller frees both buffers with
// BufferFree.
BOOLEAN
DiskTable (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
  OUT OABL_BUFFER          *Block,
  OUT GPT_HEADER           *Header,
  OUT OABL_BUFFER          *Array
);

// Writes Length bytes, whole blocks, at Lba through a page-aligned copy.
EFI_STATUS
DiskWrite (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
  IN UINT64                 Lba,
  IN CONST UINT8           *Data,
  IN UINTN                  Length
);

// Reads the table like DiskTable, then also requires a writable disk,
// valid CRCs in both copies, headers that agree on the layout
// (GptCopiesAgree) and the same entries in each. Partitions are only
// changed in a table that passes. Backup gets the backup's header.
BOOLEAN
DiskCheckedTable (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
  OUT OABL_BUFFER          *Block,
  OUT GPT_HEADER           *Header,
  OUT OABL_BUFFER          *Array,
  OUT GPT_HEADER           *Backup
);

// Whether Linux is installed on internal storage: a partition on disk after
// userdata in a table whose copies agree, or an image in userdata. The
// first call connects the storage, looks and logs the answer; later calls
// return the same answer.
BOOLEAN
DiskLinuxOnInternal (
  IN UINT32   Chip,
  IN LOG_SINK Log
);

#endif
