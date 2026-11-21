/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  FwUpdate - installs an ABL image staged in \openabl\update\ by Linux.
*/

#ifndef __EFI_FW_UPDATE_H__
#define __EFI_FW_UPDATE_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Efi/MenuHost.h"
#include "Efi/Serial.h"

#define FW_UPDATE_DIR "\\openabl\\update"

typedef enum {
  FW_UPDATE_NONE,
  // Refused, already installed, or the write failed.
  FW_UPDATE_NOT_WRITTEN,
  // Restart to run it.
  FW_UPDATE_WRITTEN,
} FW_UPDATE_OUTCOME;

// Storage must be ready and settings loaded.
FW_UPDATE_OUTCOME
FwUpdateRun (
  IN CONST MENU_HOST *Host,
  IN EFI_HANDLE      *Volumes,
  IN UINTN            Count,
  IN LOG_SINK         Log
);

#endif
