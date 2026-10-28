/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SettingsStore - named variables in the last 4 KiB of the misc partition.
*/

#include "Core/SettingsStore.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Core/Settings.h"

// Header offsets.
#define OFF_MAGIC     0
#define OFF_VERSION   8
#define OFF_SIZE      10
#define OFF_CRC       12
#define OFF_MODEL_LEN 16
#define OFF_MODEL     17

// A record: UINT8 NameLength, the name, UINT16 DataLength, the data.
#define RECORD_FIXED 3

#define MAGIC     "OABLVARS"
#define MAGIC_LEN 8

_Static_assert (
  OFF_MODEL + SETTINGS_STORE_MODEL_MAX <= SETTINGS_STORE_FIXED_SIZE,
  "model copy"
);
_Static_assert (SETTINGS_MAX_MODEL_LEN <= SETTINGS_STORE_MODEL_MAX, "model");

STATIC BOOLEAN
HasMagic (IN CONST UINT8 *Store)
{
  return CompareMem (Store + OFF_MAGIC, MAGIC, MAGIC_LEN) == 0;
}

// The CRC-32 of everything after the CRC field.
STATIC UINT32
StoreCrc (IN CONST UINT8 *Store)
{
  return CalculateCrc32 (
    Store + OFF_CRC + 4, SETTINGS_STORE_SIZE - OFF_CRC - 4
  );
}

// The magic, the size and the CRC, which every version keeps.
STATIC BOOLEAN
Sealed (IN CONST UINT8 *Store)
{
  return HasMagic (Store) &&
         ReadLe16 (Store + OFF_SIZE) == SETTINGS_STORE_SIZE &&
         ReadLe32 (Store + OFF_CRC) == StoreCrc (Store);
}

// TRUE if a record starts at At: the list hasn't ended.
STATIC BOOLEAN
IsRecord (
  IN CONST UINT8 *Store,
  IN UINTN        At
)
{
  return At < SETTINGS_STORE_SIZE && Store[At] != 0;
}

STATIC OABL_BYTES
NameAt (
  IN CONST UINT8 *Store,
  IN UINTN        At
)
{
  return BytesOf (Store + At + 1, Store[At]);
}

STATIC OABL_BYTES
DataAt (
  IN CONST UINT8 *Store,
  IN UINTN        At
)
{
  UINTN Name = Store[At];

  return BytesOf (
    Store + At + RECORD_FIXED + Name, ReadLe16 (Store + At + 1 + Name)
  );
}

// The size of the record at At.
STATIC UINTN
RecordSize (
  IN CONST UINT8 *Store,
  IN UINTN        At
)
{
  return RECORD_FIXED + Store[At] + DataAt (Store, At).Len;
}

// Where the records end.
STATIC UINTN
End (IN CONST UINT8 *Store)
{
  UINTN At = SETTINGS_STORE_FIXED_SIZE;

  while (IsRecord (Store, At)) {
    At += RecordSize (Store, At);
  }
  return At;
}

STATIC BOOLEAN
BytesEq (
  IN OABL_BYTES A,
  IN OABL_BYTES B
)
{
  return A.Len == B.Len && CompareMem (A.Ptr, B.Ptr, A.Len) == 0;
}

// Finds Name's record. 0 if it isn't there.
STATIC UINTN
Find (
  IN CONST UINT8 *Store,
  IN OABL_BYTES   Name
)
{
  for (UINTN At = SETTINGS_STORE_FIXED_SIZE; IsRecord (Store, At);
       At += RecordSize (Store, At)) {
    if (BytesEq (NameAt (Store, At), Name)) {
      return At;
    }
  }
  return 0;
}

// TRUE if the records are sound: each inside the block, its name no longer
// than SETTINGS_STORE_NAME_MAX, and the names unique.
STATIC BOOLEAN
LayoutValid (IN CONST UINT8 *Store)
{
  UINTN At = SETTINGS_STORE_FIXED_SIZE;

  if (Store[OFF_MODEL_LEN] > SETTINGS_STORE_MODEL_MAX) {
    return FALSE;
  }
  while (IsRecord (Store, At)) {
    if (Store[At] > SETTINGS_STORE_NAME_MAX ||
        At + RECORD_FIXED + Store[At] > SETTINGS_STORE_SIZE ||
        At + RecordSize (Store, At) > SETTINGS_STORE_SIZE) {
      return FALSE;
    }
    for (UINTN Other = SETTINGS_STORE_FIXED_SIZE; Other < At;
         Other += RecordSize (Store, Other)) {
      if (BytesEq (NameAt (Store, Other), NameAt (Store, At))) {
        return FALSE;
      }
    }
    At += RecordSize (Store, At);
  }
  return TRUE;
}

BOOLEAN
SettingsStoreValid (IN CONST UINT8 Store[SETTINGS_STORE_SIZE])
{
  return Sealed (Store) &&
         ReadLe16 (Store + OFF_VERSION) == SETTINGS_STORE_VERSION &&
         LayoutValid (Store);
}

BOOLEAN
SettingsStoreModel (
  IN CONST UINT8  Store[SETTINGS_STORE_SIZE],
  OUT OABL_BYTES *Value
)
{
  UINTN Length = Store[OFF_MODEL_LEN];

  if (!Sealed (Store) || Length == 0 || Length > SETTINGS_STORE_MODEL_MAX) {
    return FALSE;
  }
  *Value = BytesOf (Store + OFF_MODEL, Length);
  return TRUE;
}

VOID
SettingsStoreFormat (OUT UINT8 Store[SETTINGS_STORE_SIZE])
{
  ZeroMem (Store, SETTINGS_STORE_SIZE);
  CopyMem (Store + OFF_MAGIC, MAGIC, MAGIC_LEN);
  WriteLe16 (Store + OFF_VERSION, SETTINGS_STORE_VERSION);
  WriteLe16 (Store + OFF_SIZE, SETTINGS_STORE_SIZE);
}

// TRUE if every byte of the store is Fill.
STATIC BOOLEAN
AllOf (
  IN CONST UINT8 *Store,
  IN UINT8        Fill
)
{
  for (UINTN Index = 0; Index < SETTINGS_STORE_SIZE; Index++) {
    if (Store[Index] != Fill) {
      return FALSE;
    }
  }
  return TRUE;
}

SETTINGS_STORE_STATE
SettingsStoreOpen (IN OUT UINT8 Store[SETTINGS_STORE_SIZE])
{
  SETTINGS_STORE_STATE State;

  if (SettingsStoreValid (Store)) {
    return SETTINGS_STORE_FOUND;
  }
  if (Sealed (Store) &&
      ReadLe16 (Store + OFF_VERSION) != SETTINGS_STORE_VERSION) {
    State = SETTINGS_STORE_OTHER_VERSION;
  } else if (HasMagic (Store)) {
    State = SETTINGS_STORE_DAMAGED;
  } else if (AllOf (Store, 0x00) || AllOf (Store, 0xFF)) {
    State = SETTINGS_STORE_BLANK;
  } else {
    State = SETTINGS_STORE_REPLACED;
  }
  SettingsStoreFormat (Store);
  return State;
}

BOOLEAN
SettingsStoreNameValid (IN CONST CHAR8 *Name)
{
  UINTN Length = AsciiStrLen (Name);

  if (Length == 0 || Length > SETTINGS_STORE_NAME_MAX) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Length; Index++) {
    if ((UINT8)Name[Index] < 0x21 || (UINT8)Name[Index] > 0x7e) {
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
SettingsStoreGet (
  IN CONST UINT8  Store[SETTINGS_STORE_SIZE],
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
)
{
  UINTN At = Find (Store, BytesOf (Name, AsciiStrLen (Name)));

  if (At == 0) {
    return FALSE;
  }
  *Value = DataAt (Store, At);
  return TRUE;
}

// Removes the record at At, moving the later ones down over it.
STATIC VOID
Remove (
  IN OUT UINT8 *Store,
  IN UINTN      At
)
{
  UINTN Size = RecordSize (Store, At);
  UINTN Last = End (Store);

  CopyMem (Store + At, Store + At + Size, Last - At - Size);
}

BOOLEAN
SettingsStoreSet (
  IN OUT UINT8    Store[SETTINGS_STORE_SIZE],
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Size
)
{
  UINTN      NameLen;
  UINTN      At;
  UINTN      Last;
  OABL_BYTES Old;

  if (!SettingsStoreNameValid (Name)) {
    return FALSE;
  }
  NameLen = AsciiStrLen (Name);
  At      = Find (Store, BytesOf (Name, NameLen));
  Last    = End (Store);
  if (At != 0) {
    Old = DataAt (Store, At);
    // The same size: in place.
    if (Size != 0 && Old.Len == Size) {
      CopyMem ((UINT8 *)Old.Ptr, Data, Size);
      return TRUE;
    }
    Last -= RecordSize (Store, At);
  }
  // Check the fit before anything is removed, so a failure changes nothing.
  if (Size != 0 && Last + RECORD_FIXED + NameLen + Size > SETTINGS_STORE_SIZE) {
    return FALSE;
  }
  if (At != 0) {
    Remove (Store, At);
  }
  // Clear what follows, so the list ends after the last record.
  ZeroMem (Store + Last, SETTINGS_STORE_SIZE - Last);
  if (Size != 0) {
    Store[Last] = (UINT8)NameLen;
    CopyMem (Store + Last + 1, Name, NameLen);
    WriteLe16 (Store + Last + 1 + NameLen, (UINT16)Size);
    CopyMem (Store + Last + RECORD_FIXED + NameLen, Data, Size);
  }
  return TRUE;
}

VOID
SettingsStoreSeal (IN OUT UINT8 Store[SETTINGS_STORE_SIZE])
{
  OABL_BYTES Model;

  ZeroMem (Store + OFF_MODEL_LEN, 1 + SETTINGS_STORE_MODEL_MAX);
  if (SettingsStoreGet (Store, SETTINGS_KEY_MODEL, &Model) &&
      Model.Len <= SETTINGS_STORE_MODEL_MAX) {
    Store[OFF_MODEL_LEN] = (UINT8)Model.Len;
    CopyMem (Store + OFF_MODEL, Model.Ptr, Model.Len);
  }
  WriteLe32 (Store + OFF_CRC, StoreCrc (Store));
}
