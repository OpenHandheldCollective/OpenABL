/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Variables - OpenABL's settings, kept in the misc partition.

  Core/Settings has the names and encodings, Core/SettingsStore the layout in
  the last 4 KiB of misc. Every device keeps its settings there, so they
  don't depend on XBL's variable services or on devinfo.

  The store is read once into memory. Gets and puts work on that copy, and
  VariablesFlush writes it back, so a put is kept only once it is flushed.
*/

#ifndef __EFI_VARIABLES_H__
#define __EFI_VARIABLES_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Efi/Serial.h"

// Reads the store from misc's last 4 KiB if it isn't read yet, logging to
// Log from now on. Anything there that isn't a valid store is replaced by
// an empty one. FALSE if misc can't be found or read: the settings are then
// an empty store in memory, which flushes don't write. After a miss only a
// call with a Log tries again, and only until something is put; gets and
// puts load with LOG_QUIET.
BOOLEAN
VariablesLoad (IN LOG_SINK Log);

// Reads a setting's value. FALSE if it is unset or the store can't be read.
// Caller frees Out with BufferFree.
BOOLEAN
VariablesGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
);

// Reads a setting into Buffer from the copy in memory, without reading misc
// or allocating, so it works at ExitBootServices. FALSE if nothing has
// loaded the store yet, or the setting is unset or longer than Capacity.
BOOLEAN
VariablesPeek (
  IN CONST CHAR8 *Name,
  OUT UINT8      *Buffer,
  IN UINTN        Capacity,
  OUT UINTN      *Length
);

// Sets a setting, without flushing. Length 0 deletes it, and deleting a
// missing setting succeeds. EFI_INVALID_PARAMETER for a name the store
// can't hold and EFI_OUT_OF_RESOURCES when the store is full.
EFI_STATUS
VariablesPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Length
);

// Writes the store to misc's last 4 KiB if a put changed it. EFI_SUCCESS
// when there was nothing to write, EFI_NOT_FOUND if misc couldn't be read.
EFI_STATUS
VariablesFlush (VOID);

// Once, when misc held no store of this version: carries the device model
// over from another version's store, else from an older loader
// (MigrateModel), and writes the new store. Run early at each boot.
VOID
VariablesSetUp (IN LOG_SINK Log);

#endif
