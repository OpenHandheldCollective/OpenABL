/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SettingsStore - named variables in the last 4 KiB of the misc partition.

  Android uses the first 64 KiB of misc, which is at least 1 MiB, so the
  last 4 KiB is OpenABL's: anything there that isn't a valid store is
  replaced by an empty one.

  The store, all fields little-endian:

  - Fixed header, 128 bytes, the same in every version:
      0  Magic "OABLVARS"
      8  UINT16 Version
     10  UINT16 Size (4096)
     12  UINT32 CRC-32 (IEEE 802.3) of bytes 16 to the end
     16  UINT8  ModelLength, 0 to 96
     17  96 bytes: the device model, a copy of the DeviceModel variable
         (UTF-8, no NUL), zero-padded. Empty when it isn't set or is longer.
    113  15 reserved bytes (zero)
  - Version 1, from offset 128: records packed one after another, each a
    UINT8 NameLength (1 to SETTINGS_STORE_NAME_MAX), the name (ASCII, no
    NUL; OpenABL writes only printable names), a UINT16 DataLength and the
    data. A NameLength of 0, or the end of the block, ends the list. The
    rest is zero.

  Later versions and forks must keep the fixed header (the magic, Size, the
  CRC and the model copy, refreshed on every write) and the DeviceModel
  name and encoding, and change the rest, or what a setting means, only
  with a new Version, never a new magic. A store of another version is
  replaced keeping only the model; another magic is foreign data.

  A write that is cut short fails the CRC, and the store starts again
  empty.
*/

#ifndef __CORE_SETTINGS_STORE_H__
#define __CORE_SETTINGS_STORE_H__

#include "Base/OablBase.h"

#include "Base/Text.h"

// The store at the end of misc, and its version: bump it when the layout
// or the meaning of an existing setting changes.
#define SETTINGS_STORE_SIZE    4096
#define SETTINGS_STORE_VERSION 1

// The fixed header every version keeps, and the device model's room in it.
#define SETTINGS_STORE_FIXED_SIZE 128
#define SETTINGS_STORE_MODEL_MAX  96

// The longest setting name.
#define SETTINGS_STORE_NAME_MAX 64

// What SettingsStoreOpen found.
typedef enum {
  // A valid store.
  SETTINGS_STORE_FOUND,
  // All 0x00 or all 0xFF: now an empty store.
  SETTINGS_STORE_BLANK,
  // An older or newer version's store. Now an empty store.
  SETTINGS_STORE_OTHER_VERSION,
  // The magic, but not valid: a write was cut short or the store was
  // damaged. Now an empty store.
  SETTINGS_STORE_DAMAGED,
  // Something else's data. Now an empty store.
  SETTINGS_STORE_REPLACED,
} SETTINGS_STORE_STATE;

// Checks the 4 KiB read from the end of misc, making it an empty store
// unless it is a valid one.
SETTINGS_STORE_STATE
SettingsStoreOpen (IN OUT UINT8 Store[SETTINGS_STORE_SIZE]);

// Makes Store an empty store.
VOID
SettingsStoreFormat (OUT UINT8 Store[SETTINGS_STORE_SIZE]);

// TRUE if Store is a valid store of this version.
BOOLEAN
SettingsStoreValid (IN CONST UINT8 Store[SETTINGS_STORE_SIZE]);

// Reads the model copy in the fixed header of any version's store. FALSE
// unless the magic, size and CRC are good and a model is set. Value points
// into Store.
BOOLEAN
SettingsStoreModel (
  IN CONST UINT8  Store[SETTINGS_STORE_SIZE],
  OUT OABL_BYTES *Value
);

// TRUE if Name can be stored: 1 to SETTINGS_STORE_NAME_MAX printable ASCII
// characters.
BOOLEAN
SettingsStoreNameValid (IN CONST CHAR8 *Name);

// Finds variable Name. Value points into Store.
BOOLEAN
SettingsStoreGet (
  IN CONST UINT8  Store[SETTINGS_STORE_SIZE],
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
);

// Sets variable Name, or deletes it if Size is 0 (deleting a missing one
// succeeds). FALSE, leaving Store as it was, if the name isn't valid or the
// value doesn't fit.
BOOLEAN
SettingsStoreSet (
  IN OUT UINT8    Store[SETTINGS_STORE_SIZE],
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Size
);

// Copies the DeviceModel variable into the fixed header and sets the CRC,
// ready for writing.
VOID
SettingsStoreSeal (IN OUT UINT8 Store[SETTINGS_STORE_SIZE]);

#endif
