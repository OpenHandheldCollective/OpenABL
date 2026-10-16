/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  NestedDisk - a disk image written to userdata, published as a disk of its
  own so XBL's partition and FAT drivers mount its ESP. Most XBLs skip a GPT
  inside a partition.
*/

#ifndef __EFI_NESTED_DISK_H__
#define __EFI_NESTED_DISK_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Core/Gpt.h"
#include "Efi/Serial.h"

typedef struct _NESTED_GPT {
  EFI_HANDLE             Handle;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  UINT32                 SectorSize;
  GPT_HEADER             Header;
  OABL_BUFFER            Array;
} NESTED_GPT;

// Finds userdata holding a GPT with valid CRCs
BOOLEAN
NestedDiskFind (OUT NESTED_GPT *Out);

// Publishes userdata as a read only disk when it holds an image
VOID
NestedDiskPublish (IN LOG_SINK Log);

#endif
