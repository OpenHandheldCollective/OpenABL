/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Uninstall - finds Linux on internal storage and removes it.

  Linux is on the first writable fixed disk whose GPT, with both copies
  valid and the same, has a partition on disk after userdata. Uninstalling
  rewrites that GPT (GptUninstall) and asks recovery, through misc, to wipe
  Android's data so it fills the space. Block I/O goes through page-aligned
  buffers, which satisfy any IoAlign.
*/

#ifndef __EFI_UNINSTALL_H__
#define __EFI_UNINSTALL_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Core/Gpt.h"
#include "Efi/Serial.h"

// Opens a disk's BlockIo shared, leaving the disk with its drivers. NULL if
// there is none.
EFI_BLOCK_IO_PROTOCOL *
UninstallBlockIo (IN EFI_HANDLE Handle);

// Reads Length bytes from Lba. Caller frees Out with BufferFree.
BOOLEAN
UninstallRead (
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
UninstallTable (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
  OUT OABL_BUFFER          *Block,
  OUT GPT_HEADER           *Header,
  OUT OABL_BUFFER          *Array
);

// Whether Linux is installed on internal storage, after userdata or as an
// image in it. The first call connects the storage
//looks and logs the answer; later calls return the,same answer.
BOOLEAN
UninstallCfwOnInternal (
  IN UINT32   Chip,
  IN LOG_SINK Log
);

// At most this many of Linux's partitions are listed for the confirmation.
#define UNINSTALL_MAX_PARTS 8

// Points Parts at the partitions UninstallCfwOnInternal found, and returns
// how many (none before it ran, or after an uninstall).
UINTN
UninstallCfwParts (OUT CONST GPT_CFW_PART **Parts);

// Removes the Linux partitions from internal storage and gives their space
// to userdata, in both GPTs, flushing each write, then reads both back.
// EFI_NOT_FOUND if Linux isn't there, EFI_VOLUME_CORRUPTED if the table
// can't be trusted (nothing was written), EFI_CRC_ERROR if the tables read
// back wrong, else the write error. Block I/O writes return none of those
// three, so the menu can say which step failed.
EFI_STATUS
UninstallRun (IN LOG_SINK Log);

// Writes the bootloader message asking recovery to wipe data to misc. The
// caller then sets Android as the boot mode and restarts into recovery.
EFI_STATUS
UninstallRequestWipe (IN LOG_SINK Log);

#endif
