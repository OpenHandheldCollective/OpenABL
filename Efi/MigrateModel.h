/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  MigrateModel - finds the device model older loaders stored, so the first
  boot with the settings in misc keeps it. Nothing else is carried over.

  The model was kept in one of four places, tried in this order:

  - The DeviceModel setting, under the vendor GUID
    8c5f2b7e-3a41-4d6c-9e2f-5b7a1c0d4e93, by OpenABLs that kept the
    settings in the UEFI variable store.
  - devinfo's persistent values OablModel0 to OablModel2, 32 bytes each,
    joined in that order: the copy OpenABL's SDM845 builds kept, as XBL had
    no variable services.
  - devinfo's persistent value ModelSelection, written with its NUL by
    ROCKNIX's LinuxLoader (SetDeviceModelSelection) and the first OpenABLs,
    and cut at the NUL.
  - devinfo's model_selection field (64 bytes, cut at the NUL), which
    ROCKNIX's LinuxLoader kept after the end of the struct before it used
    persistent values. It is read only if VerifiedBoot reads the 3408-byte
    record and the bytes there look like ROCKNIX's old fields.

  devinfo is LinuxLoader's DeviceInfo struct, read with VerifiedBoot's
  VBRwDeviceState. Its eight persistent values start at offset 2456, at the
  struct's natural AArch64 alignment.

  Once a found model is safe in misc, MigrateModelCleanUp removes what the
  older loaders left: every setting under that vendor GUID, the settings
  ROCKNIX's LinuxLoader and OpenABL kept in devinfo's persistent values,
  and the fields ROCKNIX's LinuxLoader once added after the end of the
  struct. The ABL's own devinfo fields, the charger-screen flag among them,
  are kept.
*/

#ifndef __EFI_MIGRATE_MODEL_H__
#define __EFI_MIGRATE_MODEL_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"
#include "Efi/Serial.h"

#include "Core/Settings.h"

// Finds the model an older loader stored. From names where it was found,
// for the log. FALSE if none of the places holds one.
BOOLEAN
MigrateModel (
  OUT UINT8         Out[SETTINGS_MAX_MODEL_LEN],
  OUT UINTN        *Length,
  OUT CONST CHAR8 **From
);

// Removes the settings older loaders stored, logging what it removed.
// Call it only once the model MigrateModel found is written to misc.
VOID
MigrateModelCleanUp (IN LOG_SINK Log);

#endif
