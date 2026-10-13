/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Efi - UEFI declarations, global tables and small helpers.

  The declarations come from the edk2 packages in the ABL's tree (fetched
  into build/edk2), so we share OpenAblMenuHost.h with the ABL. Common GUIDs
  are defined in Efi.c; a GUID used by one module lives in that module.
*/

#ifndef __EFI_EFI_H__
#define __EFI_EFI_H__

#include "Base/OablBase.h"

#include <Uefi.h>

#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Guid/FileSystemVolumeLabelInfo.h>
#include <Guid/GlobalVariable.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DevicePathToText.h>
#include <Protocol/DiskIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadFile2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SerialIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>

extern EFI_HANDLE            gImageHandle;
extern EFI_SYSTEM_TABLE     *gST;
extern EFI_BOOT_SERVICES    *gBS;
extern EFI_RUNTIME_SERVICES *gRT;

// Saves the image handle and the tables. Call it first.
VOID
UefiInit (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
);

// Gets the protocol on Handle with EFI_OPEN_PROTOCOL_GET_PROTOCOL, so there
// is nothing to close. Returns NULL if the handle doesn't have it.
VOID *
GetProtocol (
  IN EFI_HANDLE Handle OPTIONAL,
  IN EFI_GUID  *Guid
);

// Returns the first instance of a protocol, or NULL.
VOID *
LocateProtocol (IN EFI_GUID *Guid);

// Gets the handles with the protocol and returns their count. Caller frees
// Out with gBS->FreePool.
UINTN
LocateHandles (
  IN EFI_GUID     *Guid,
  OUT EFI_HANDLE **Out
);

BOOLEAN
CompareGuid (
  IN CONST EFI_GUID *Guid1,
  IN CONST EFI_GUID *Guid2
);

// Starts Image and frees any exit data it returns.
EFI_STATUS
StartImageFreeExitData (IN EFI_HANDLE Image);

// Gets the memory map: *Count descriptors, *DescriptorSize bytes apart.
// Caller frees it with FreePool. NULL on failure.
UINT8 *
MemoryMapGet (
  OUT UINTN *Count,
  OUT UINTN *DescriptorSize
);

// Descriptor Index of a map from MemoryMapGet.
#define MEMORY_MAP_AT(Map, DescriptorSize, Index)                              \
  ((CONST EFI_MEMORY_DESCRIPTOR *)((Map) + (Index) * (DescriptorSize)))

// Room for "warning 65535" and the terminator.
#define STATUS_TEXT_SIZE 16

// Formats Status for a log line as "ok", "error 14" or "warning 3" (low 16
// bits in decimal). Returns Out, so it can be passed straight to a format:
// StatusText (St, (CHAR8[STATUS_TEXT_SIZE]){0}).
CONST CHAR8 *
StatusText (
  IN EFI_STATUS Status,
  OUT CHAR8     Out[STATUS_TEXT_SIZE]
);

// Monotonic time from the AArch64 architected timer. Boot services have no
// cheap clock: GetTime is slow and often has one-second resolution.

// Returns the virtual counter in milliseconds, or 0 if the frequency is under
// 1 kHz.
UINT64
ClockNowMs (VOID);

// Returns microseconds since the counter started, usually at reset, for the
// Boot Loader Interface's LoaderTime*USec variables. Returns 0 if the
// frequency reads 0.
UINT64
ClockNowUs (VOID);

// Connects every driver to every controller, recursively, as a full boot
// manager would. Firmware that boots "fast" connects only the boot device,
// which leaves other drives without a file system.
VOID
InventoryConnectAll (VOID);

#endif
