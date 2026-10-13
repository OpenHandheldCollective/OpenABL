/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DefaultFiles - boots the removable-media default file.

  Each drive is searched in order for \EFI\BOOT\BOOTAA64.EFI or
  BOOTARM.EFI. The image is loaded by device path, or from a raw FAT32 read
  when XBL's FAT driver can't find or load it. The first image started ends
  the search, whatever it returns.
*/

#ifndef __EFI_DEFAULT_FILES_H__
#define __EFI_DEFAULT_FILES_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

extern CONST CHAR8 *CONST gDefaultFilesPaths[2];

// Callbacks from DefaultFilesBoot. Each is passed Ctx.
typedef struct _DEFAULT_FILES_HOST {
  // Called just before the image from Fs starts.
  VOID (*BeforeStart) (
    IN VOID      *Ctx,
    IN EFI_HANDLE Fs
  );
  // Called when the started image returns.
  VOID (*AfterReturn) (IN VOID *Ctx);
  VOID (*Log) (
    IN VOID        *Ctx,
    IN CONST CHAR8 *Line
  );
  VOID *Ctx;
} DEFAULT_FILES_HOST;

typedef enum {
  // No drive has one, or none loaded.
  DEFAULT_FILES_OUTCOME_NOT_FOUND,
  // One started. *Returned has its exit status.
  DEFAULT_FILES_OUTCOME_RETURNED,
} DEFAULT_FILES_OUTCOME;

// Boots the first default file on Drives. An image that returns an error is
// unloaded.
DEFAULT_FILES_OUTCOME
DefaultFilesBoot (
  IN CONST EFI_HANDLE         *Drives,
  IN UINTN                     Count,
  IN CONST DEFAULT_FILES_HOST *Host,
  OUT EFI_STATUS              *Returned OPTIONAL
);

#endif
