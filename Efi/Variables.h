/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Variables - OpenABL's settings as UEFI non-volatile variables.

  Core/Settings has the names and encodings. XBL keeps variables in the
  TrustZone variable app, which writes them to the varstore partition only
  when flushed (QcomFlushVariables). SDM845's XBL has no variable services,
  so nothing flushes them; with OABL_DEVINFO_SETTINGS they are also packed
  into devinfo at each flush and restored from it at start.
*/

#ifndef __EFI_VARIABLES_H__
#define __EFI_VARIABLES_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Efi/Serial.h"

// The settings' vendor GUID, shared by the ABL and the menu:
// 8c5f2b7e-3a41-4d6c-9e2f-5b7a1c0d4e93.
#define SETTINGS_VENDOR_GUID                                                   \
  {0x8c5f2b7e, 0x3a41, 0x4d6c, {0x9e, 0x2f, 0x5b, 0x7a, 0x1c, 0x0d, 0x4e, 0x93}}

// Reads a setting's value. FALSE if it is unset or unreadable. Caller frees
// Out with BufferFree.
BOOLEAN
VariablesGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
);

// Writes a setting as a non-volatile variable, without flushing. Length 0
// deletes it, and deleting a missing setting succeeds. EFI_INVALID_PARAMETER
// for a name UCS-2 can't hold.
EFI_STATUS
VariablesPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Length
);

// Moves the OffmodeCharging setting into devinfo's charger screen flag,
// which the ABL reads as stock Qualcomm code does. Run once, before the ABL
// reads devinfo. The variable is then deleted and flushed.
// If devinfo isn't written yet, the variable stays for the next boot.
VOID
VariablesChargerScreenToDevinfo (IN LOG_SINK Log);

// Moves the settings out of devinfo, where older ABLs kept them, into
// variables. Run once a boot, before the ABL reads devinfo. Where devinfo
// also holds the settings' copy, its old values are cleared every time, in
// case a move was cut short or an older ABL booted since; elsewhere they
// stay for an older ABL flashed back. If the variables can't be written,
// or devinfo can't be read, the settings stay in devinfo for the next
// boot.
VOID
VariablesMoveOutOfDevinfo (IN LOG_SINK Log);

#ifdef OABL_DEVINFO_SETTINGS

// Packs the variables into devinfo, in place of a flush where XBL has no
// variable services. EFI_UNSUPPORTED without VerifiedBoot or a record,
// EFI_OUT_OF_RESOURCES when devinfo had no room, EFI_WARN_WRITE_FAILURE
// when a variable didn't fit.
EFI_STATUS
VariablesPersistToDevinfo (VOID);

// Restores the variables from devinfo's copy at start, where XBL has no
// variable services and the last restart lost them.
VOID
VariablesRestoreFromDevinfo (IN LOG_SINK Log);

#endif

#endif
