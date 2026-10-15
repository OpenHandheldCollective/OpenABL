/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootImage - finds and starts an Android boot image.

  The image is read into page-rounded, zeroed pool memory. After a
  successful SetBootImage the ABL owns it; otherwise the caller frees it
  with gBS->FreePool. BootImageStart lays it out as Qualcomm's BootLinux
  does, then the ABL updates the devicetree and jumps to the kernel.
*/

#ifndef __EFI_BOOT_IMAGE_H__
#define __EFI_BOOT_IMAGE_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Efi/DefaultFiles.h"
#include "Efi/MenuHost.h"
#include "Efi/Serial.h"

#include "Base/Buffer.h"

// A boot image in pool memory.
typedef struct _BOOT_IMAGE_FOUND {
  UINT8 *Buffer;
  UINTN  Size;
  // Found on a USB drive, so USB is handed over to the OS before boot.
  BOOLEAN Usb;
} BOOT_IMAGE_FOUND;

// Finds the first of gModelsBootImages on Drives, in order. If XBL's FAT
// driver can't read or can't see the file, it is read straight off FAT32.
BOOLEAN
BootImageFind (
  IN CONST EFI_HANDLE         *Drives,
  IN UINTN                     Count,
  IN CONST DEFAULT_FILES_HOST *Host,
  OUT BOOT_IMAGE_FOUND        *Out
);

// Why BootImageStart returned.
typedef enum {
  // No device model is selected, so there is no devicetree. The image still
  // belongs to the ABL.
  BOOT_IMAGE_NOT_STARTED_NO_MODEL,
  // Verified boot refused it. *Refused has the status.
  BOOT_IMAGE_NOT_STARTED_REFUSED,
  // Laying it out failed, or the ABL couldn't start it.
  BOOT_IMAGE_NOT_STARTED_FAILED,
} BOOT_IMAGE_NOT_STARTED;

// Starts the boot image passed to SetBootImage. Verified boot
// (OPENABL_MENU_ACTION_BOOT_IMAGE_VERIFY) checks it first; after that the
// buffer is ours again and is freed here in every case. Model selects the
// devicetree from those appended to the kernel. Returns only if the kernel
// didn't start. *Refused gets verified boot's status for
// BOOT_IMAGE_NOT_STARTED_REFUSED.
BOOT_IMAGE_NOT_STARTED
BootImageStart (
  IN CONST MENU_HOST        *Host,
  IN CONST BOOT_IMAGE_FOUND *Found,
  IN CONST CHAR8            *Model OPTIONAL,
  IN LOG_SINK                Log,
  OUT EFI_STATUS            *Refused OPTIONAL
);

// Like BootImageStart, for an image that is already verified (fastboot's
// boot command in Linux boot mode). The buffer stays the caller's. Returns
// only on failure: EFI_NOT_READY without a model, else EFI_LOAD_ERROR.
EFI_STATUS
BootImageStartVerified (
  IN CONST MENU_HOST        *Host,
  IN CONST BOOT_IMAGE_FOUND *Found,
  IN CONST CHAR8            *Model OPTIONAL,
  IN LOG_SINK                Log
);

// CmdLine: kernel command line additions from files on the volumes and from
// the panel XBL configured. Core/KernelCmdLine holds the rules.

// Builds the command line for a Linux boot from Base, the boot image's
// command line, and a command-line file. The first non-empty override file
// (KERNEL_CMDLINE_OVERRIDE) on any volume wins, else the first append file.
// Files over 64 KiB are skipped. The result goes into the empty buffer Out,
// unterminated; caller frees it. Returns FALSE if there is no file.
BOOLEAN
CmdLineGet (
  IN CONST UINT8     *Base,
  IN UINTN            Length,
  IN LOG_SINK         Log,
  IN OUT OABL_BUFFER *Out
);

// Appends the OEM androidboot ids.
VOID
CmdLineAndroidParams (
  IN LOG_SINK         Log,
  IN OUT OABL_BUFFER *Out
);

#endif
