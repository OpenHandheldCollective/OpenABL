/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SettingsStore - Settings copy in devinfo, for XBL without variable services.

  On SDM845 nothing commits UEFI variables to flash. Efi/Variables.c writes
  this copy where it would flush them, and reads it back at start.

  devinfo has 8 persistent values of 32 bytes, so the settings are packed:

  - OablSettings: byte 0 is the format (1), byte 1 SettingsSchema, bytes 2-9
    a little-endian bitfield with every small setting, bytes 10-13 the
    FNV-1a hash of the model DeviceModelRotation was looked up for, and
    bytes 14-31 are reserved (zero). Each setting in the bitfield is 0 when
    unset, else 1 + its value. Bits 37-63 are free. Later formats only add
    fields, so older code can still read a newer copy.
  - OablModel0 to OablModel2: the device model name, 32 bytes each.
*/

#ifndef __CORE_SETTINGS_STORE_H__
#define __CORE_SETTINGS_STORE_H__

#include "Base/OablBase.h"

#include "Base/Text.h"
#include "Core/Settings.h"

#define SETTINGS_STORE_ENTRY "OablSettings"
// The model name is split over these entries.
#define SETTINGS_STORE_MODEL_ENTRIES 3
extern CONST CHAR8 *CONST
  gSettingsStoreModelEntries[SETTINGS_STORE_MODEL_ENTRIES];

// Values SettingsStorePack writes: OablSettings, then the model entries.
#define SETTINGS_STORE_ENTRIES (1 + SETTINGS_STORE_MODEL_ENTRIES)
// Most variables that can fail to fit: each small setting and the model.
#define SETTINGS_STORE_SKIPPED_MAX 15
// Each small setting, the schema, the model and DeviceModelRotationFor.
#define SETTINGS_STORE_VARS 17

// A devinfo value of the copy. Len 0 clears the entry.
typedef struct _SETTINGS_STORE_VALUE {
  CONST CHAR8 *Name;
  UINT8        Value[32];
  UINTN        Len;
} SETTINGS_STORE_VALUE;

// Reads devinfo's persistent value Name. Returns FALSE if it isn't set.
typedef BOOLEAN
SETTINGS_STORE_VALUE_OF (
  IN VOID        *Context,
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
);

// Packs the variables into devinfo values, listing any that don't fit.
VOID
SettingsStorePack (
  IN SETTINGS_GET         *Get,
  IN VOID                 *Context OPTIONAL,
  OUT SETTINGS_STORE_VALUE Out[SETTINGS_STORE_ENTRIES],
  OUT CONST CHAR8         *Skipped[SETTINGS_STORE_SKIPPED_MAX],
  OUT UINTN               *SkippedCount
);

// Unpacks the copy into variables. An empty value deletes the variable.
BOOLEAN
SettingsStoreUnpack (
  IN SETTINGS_STORE_VALUE_OF *ValueOf,
  IN VOID                    *Context OPTIONAL,
  OUT SETTINGS_VAR            Out[SETTINGS_STORE_VARS]
);

// Returns TRUE if the copy holds the variable Key.
BOOLEAN
SettingsStoreCovers (IN CONST CHAR8 *Key);

#endif
