/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Uninstall - finds Linux on internal storage and removes it.

  Linux is on the first fixed disk whose GPT has a partition after
  userdata. Uninstalling rewrites that GPT (GptUninstall). Block I/O goes
  through page-aligned buffers, which satisfy any IoAlign.
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

// Whether Linux is installed on internal storage. The first call connects
// the storage (QuirksConnectAll), looks and logs the answer; later calls
// return the same answer.
BOOLEAN
UninstallCfwOnInternal (
  IN UINT32   Chip,
  IN LOG_SINK Log
);

// Removes the Linux partitions from internal storage and gives their space
// to userdata, in both GPTs, then flushes. The caller restarts.
// EFI_NOT_FOUND if Linux isn't there, else the write or flush error.
EFI_STATUS
UninstallRun (IN LOG_SINK Log);

#endif
