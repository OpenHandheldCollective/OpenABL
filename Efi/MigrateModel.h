/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  MigrateModel - finds the device model older loaders stored, so the first
  boot with the settings in misc keeps it. Nothing else is carried over.

  The model was kept in one of three places, tried in this order:

  - The DeviceModel UEFI variable, under the vendor GUID
    8c5f2b7e-3a41-4d6c-9e2f-5b7a1c0d4e93, by OpenABLs that kept the
    settings in UEFI variables.
  - devinfo's persistent values OablModel0 to OablModel2, 32 bytes each,
    joined in that order: the copy OpenABL's SDM845 builds kept, as XBL had
    no variable services.
  - devinfo's persistent value ModelSelection, written with its NUL by
    ROCKNIX's LinuxLoader (SetDeviceModelSelection) and the first OpenABLs,
    and cut at the NUL.

  devinfo is LinuxLoader's DeviceInfo struct, read (never written) with
  VerifiedBoot's VBRwDeviceState. Its eight persistent values start at
  offset 2456, at the struct's natural AArch64 alignment.
*/

#ifndef __EFI_MIGRATE_MODEL_H__
#define __EFI_MIGRATE_MODEL_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Core/Settings.h"

// Finds the model an older loader stored. From names where it was found,
// for the log. FALSE if none of the places holds one.
BOOLEAN
MigrateModel (
  OUT UINT8         Out[SETTINGS_MAX_MODEL_LEN],
  OUT UINTN        *Length,
  OUT CONST CHAR8 **From
);

#endif
